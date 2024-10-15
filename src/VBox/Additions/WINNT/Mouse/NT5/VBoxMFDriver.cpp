/* $Id: VBoxMFDriver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Mouse Filter Driver - Interface functions.
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

#include "VBoxMF.h"
#include <VBox/VBoxGuestLib.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>

#ifdef ALLOC_PRAGMA
# pragma alloc_text(INIT, DriverEntry)
#endif

/* Driver entry point */
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    NOREF(RegistryPath);
    PAGED_CODE();
LOGREL(("DriverEntry:"));

    int irc = RTR0Init(0);
    if (RT_FAILURE(irc))
    {
        LOGREL(("failed to init IPRT (rc=%#x)", irc));
        return STATUS_INTERNAL_ERROR;
    }
    LOGF_ENTER();

    DriverObject->DriverUnload = VBoxDrvUnload;
    DriverObject->DriverExtension->AddDevice = VBoxDrvAddDevice;

    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i)
        DriverObject->MajorFunction[i] = VBoxIrpPassthrough;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = VBoxIrpInternalIOCTL;
    DriverObject->MajorFunction[IRP_MJ_PNP]                     = VBoxIrpPnP;
    DriverObject->MajorFunction[IRP_MJ_POWER]                   = VBoxIrpPower;

    VBoxMouFltInitGlobals();
    LOGF_LEAVE();
    return STATUS_SUCCESS;
}

VOID VBoxDrvUnload(IN PDRIVER_OBJECT Driver)
{
    NOREF(Driver);
    PAGED_CODE();
    LOGF_ENTER();

    VBoxMouFltDeleteGlobals();
    RTR0Term();
}

#define VBOXUSB_RLTAG 'LRBV'

NTSTATUS VBoxDrvAddDevice(IN PDRIVER_OBJECT Driver, IN PDEVICE_OBJECT PDO)
{
    NTSTATUS rc;
    PDEVICE_OBJECT pDO, pDOParent;
    PVBOXMOUSE_DEVEXT pDevExt;

    PAGED_CODE();
    LOGF_ENTER();

    rc = IoCreateDevice(Driver, sizeof(VBOXMOUSE_DEVEXT), NULL, FILE_DEVICE_MOUSE, 0, FALSE, &pDO);
    if (!NT_SUCCESS(rc))
    {
        WARN(("IoCreateDevice failed with %#x", rc));
        return rc;
    }

    pDevExt = (PVBOXMOUSE_DEVEXT) pDO->DeviceExtension;
    RtlZeroMemory(pDevExt, sizeof(VBOXMOUSE_DEVEXT));

    IoInitializeRemoveLock(&pDevExt->RemoveLock, VBOXUSB_RLTAG, 1, 100);

    rc = IoAcquireRemoveLock(&pDevExt->RemoveLock, pDevExt);
    if (!NT_SUCCESS(rc))
    {
        WARN(("IoAcquireRemoveLock failed with %#x", rc));
        IoDeleteDevice(pDO);
        return rc;
    }

    pDOParent = IoAttachDeviceToDeviceStack(pDO, PDO);
    if (!pDOParent)
    {
        IoReleaseRemoveLockAndWait(&pDevExt->RemoveLock, pDevExt);

        WARN(("IoAttachDeviceToDeviceStack failed"));
        IoDeleteDevice(pDO);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    pDevExt->pdoMain   = PDO;
    pDevExt->pdoSelf   = pDO;
    pDevExt->pdoParent = pDOParent;

    VBoxDeviceAdded(pDevExt);

    pDO->Flags |= (DO_BUFFERED_IO | DO_POWER_PAGABLE);
    pDO->Flags &= ~DO_DEVICE_INITIALIZING;

    LOGF_LEAVE();
    return rc;
}

NTSTATUS VBoxIrpPassthrough(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PVBOXMOUSE_DEVEXT pDevExt;
    LOGF_ENTER();

    pDevExt = (PVBOXMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);

    LOGF_LEAVE();
    return IoCallDriver(pDevExt->pdoParent, Irp);
}

static void
VBoxServiceCB(PDEVICE_OBJECT DeviceObject, PMOUSE_INPUT_DATA InputDataStart,
              PMOUSE_INPUT_DATA InputDataEnd, PULONG InputDataConsumed)
{
    PVBOXMOUSE_DEVEXT pDevExt;
    LOGF_ENTER();

    pDevExt = (PVBOXMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    VBoxDrvNotifyServiceCB(pDevExt, InputDataStart, InputDataEnd, InputDataConsumed);

    LOGF_LEAVE();
}

NTSTATUS VBoxIrpInternalIOCTL(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PIO_STACK_LOCATION pStack;
    PVBOXMOUSE_DEVEXT pDevExt;
    LOGF_ENTER();

    pStack = IoGetCurrentIrpStackLocation(Irp);
    pDevExt = (PVBOXMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    LOGF(("IOCTL %08X, fn = %#04X", pStack->Parameters.DeviceIoControl.IoControlCode,
          (pStack->Parameters.DeviceIoControl.IoControlCode>>2)&0xFFF));

    /* Hook into connection between mouse class device and port drivers */
    if (pStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_MOUSE_CONNECT)
    {
        Irp->IoStatus.Information = 0;

        if (pDevExt->OriginalConnectData.pfnServiceCB)
        {
            WARN(("STATUS_SHARING_VIOLATION"));
            Irp->IoStatus.Status = STATUS_SHARING_VIOLATION;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Irp->IoStatus.Status;
        }

        if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(INTERNAL_MOUSE_CONNECT_DATA))
        {
            WARN(("STATUS_INVALID_PARAMETER"));
            Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Irp->IoStatus.Status;
        }

        PINTERNAL_MOUSE_CONNECT_DATA pData = (PINTERNAL_MOUSE_CONNECT_DATA) pStack->Parameters.DeviceIoControl.Type3InputBuffer;

        pDevExt->OriginalConnectData = *pData;
        pData->pDO = pDevExt->pdoSelf;
        pData->pfnServiceCB = VBoxServiceCB;
    }

    VBoxInformHost(pDevExt);

    LOGF_LEAVE();
    return VBoxIrpPassthrough(DeviceObject, Irp);
}

NTSTATUS VBoxIrpPnP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PIO_STACK_LOCATION pStack;
    PVBOXMOUSE_DEVEXT pDevExt;
    NTSTATUS rc;
    LOGF_ENTER();

    pStack = IoGetCurrentIrpStackLocation(Irp);
    pDevExt = (PVBOXMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    switch (pStack->MinorFunction)
    {
        case IRP_MN_REMOVE_DEVICE:
        {
            LOGF(("IRP_MN_REMOVE_DEVICE"));

            IoReleaseRemoveLockAndWait(&pDevExt->RemoveLock, pDevExt);

            VBoxDeviceRemoved(pDevExt);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            rc = VBoxIrpPassthrough(DeviceObject, Irp);

            IoDetachDevice(pDevExt->pdoParent);
            IoDeleteDevice(DeviceObject);
            break;
        }
        default:
        {
            rc = VBoxIrpPassthrough(DeviceObject, Irp);
            break;
        }
    }

    if (!NT_SUCCESS(rc) && rc != STATUS_NOT_SUPPORTED)
    {
        WARN(("rc=%#x", rc));
    }

    LOGF_LEAVE();
    return rc;
}

NTSTATUS VBoxIrpPower(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PVBOXMOUSE_DEVEXT pDevExt;
    PAGED_CODE();
    LOGF_ENTER();
    pDevExt = (PVBOXMOUSE_DEVEXT) DeviceObject->DeviceExtension;
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    LOGF_LEAVE();
    return PoCallDriver(pDevExt->pdoParent, Irp);
}

