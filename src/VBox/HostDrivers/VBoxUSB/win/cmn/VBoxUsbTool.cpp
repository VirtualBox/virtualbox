/* $Id: VBoxUsbTool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Windows USB R0 Tooling.
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

#define INITGUID
#include "VBoxUsbTool.h"
#include <usbbusif.h>

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <VBox/log.h>
#include <VBox/usblib.h>

#include "../../../win/VBoxDbgLog.h"

#define VBOXUSBTOOL_MEMTAG 'TUBV'

static PVOID vboxUsbToolMemAlloc(SIZE_T cbBytes)
{
    PVOID pvMem = ExAllocatePoolWithTag(NonPagedPool, cbBytes, VBOXUSBTOOL_MEMTAG);
    Assert(pvMem);
    return pvMem;
}

static PVOID vboxUsbToolMemAllocZ(SIZE_T cbBytes)
{
    PVOID pvMem = vboxUsbToolMemAlloc(cbBytes);
    if (pvMem)
    {
        RtlZeroMemory(pvMem, cbBytes);
    }
    return pvMem;
}

static VOID vboxUsbToolMemFree(PVOID pvMem)
{
    ExFreePoolWithTag(pvMem, VBOXUSBTOOL_MEMTAG);
}

VBOXUSBTOOL_DECL(PURB) VBoxUsbToolUrbAlloc(USHORT u16Function, USHORT cbSize)
{
    PURB pUrb = (PURB)vboxUsbToolMemAlloc(cbSize);
    Assert(pUrb);
    if (!pUrb)
        return NULL;

    pUrb->UrbHeader.Length = cbSize;
    pUrb->UrbHeader.Function = u16Function;
    return pUrb;
}

VBOXUSBTOOL_DECL(PURB) VBoxUsbToolUrbAllocZ(USHORT u16Function, USHORT cbSize)
{
    PURB pUrb = (PURB)vboxUsbToolMemAllocZ(cbSize);
    Assert(pUrb);
    if (!pUrb)
        return NULL;

    pUrb->UrbHeader.Length = cbSize;
    pUrb->UrbHeader.Function = u16Function;
    return pUrb;
}

VBOXUSBTOOL_DECL(PURB) VBoxUsbToolUrbReinit(PURB pUrb, USHORT cbSize, USHORT u16Function)
{
    Assert(pUrb->UrbHeader.Length == cbSize);
    if (pUrb->UrbHeader.Length < cbSize)
        return NULL;
    pUrb->UrbHeader.Length = cbSize;
    pUrb->UrbHeader.Function = u16Function;
    return pUrb;
}

VBOXUSBTOOL_DECL(VOID) VBoxUsbToolUrbFree(PURB pUrb)
{
    vboxUsbToolMemFree(pUrb);
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolUrbPost(PDEVICE_OBJECT pDevObj, PURB pUrb, ULONG dwTimeoutMs)
{
    if (dwTimeoutMs == RT_INDEFINITE_WAIT)
        return VBoxUsbToolIoInternalCtlSendSync(pDevObj, IOCTL_INTERNAL_USB_SUBMIT_URB, pUrb, NULL);
    return VBoxUsbToolIoInternalCtlSendSyncWithTimeout(pDevObj, IOCTL_INTERNAL_USB_SUBMIT_URB, pUrb, NULL, dwTimeoutMs);
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetDescriptor(PDEVICE_OBJECT pDevObj, void *pvBuffer, int cbBuffer, int Type, int iIndex, int LangId, ULONG dwTimeoutMs)
{
    NTSTATUS Status;
    USHORT cbUrb = sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
    PURB pUrb = VBoxUsbToolUrbAllocZ(URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, cbUrb);
    if (!pUrb)
    {
        WARN(("allocating URB failed"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PUSB_COMMON_DESCRIPTOR pCmn = (PUSB_COMMON_DESCRIPTOR)pvBuffer;
    pCmn->bLength = cbBuffer;
    pCmn->bDescriptorType = Type;

    pUrb->UrbHeader.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);
    pUrb->UrbControlDescriptorRequest.TransferBufferLength = cbBuffer;
    pUrb->UrbControlDescriptorRequest.TransferBuffer       = pvBuffer;
    pUrb->UrbControlDescriptorRequest.Index                = (UCHAR)iIndex;
    pUrb->UrbControlDescriptorRequest.DescriptorType       = (UCHAR)Type;
    pUrb->UrbControlDescriptorRequest.LanguageId           = (USHORT)LangId;

    Status = VBoxUsbToolUrbPost(pDevObj, pUrb, dwTimeoutMs);
    ASSERT_WARN(Status == STATUS_SUCCESS, ("VBoxUsbToolUrbPost failed Status (0x%x)", Status));

    VBoxUsbToolUrbFree(pUrb);

    return Status;
}

VBOXUSBTOOL_DECL(VOID) VBoxUsbToolStringDescriptorToUnicodeString(PUSB_STRING_DESCRIPTOR pDr, PUNICODE_STRING pUnicode)
{
    /* for some reason the string dr sometimes contains a non-null terminated string
     * although we zeroed up the complete descriptor buffer
     * this is why RtlInitUnicodeString won't work
     * we need to init the scting length based on dr length */
    pUnicode->Buffer = pDr->bString;
    pUnicode->Length = pUnicode->MaximumLength = pDr->bLength - RT_OFFSETOF(USB_STRING_DESCRIPTOR, bString);
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetStringDescriptor(PDEVICE_OBJECT pDevObj, char *pszResult, ULONG cbResult,
                                                          int iIndex, int LangId, ULONG dwTimeoutMs)
{
    char aBuf[MAXIMUM_USB_STRING_LENGTH];
    AssertCompile(sizeof (aBuf) <= UINT8_MAX);
    UCHAR cbBuf = (UCHAR)sizeof (aBuf);
    PUSB_STRING_DESCRIPTOR pDr = (PUSB_STRING_DESCRIPTOR)&aBuf;

    Assert(pszResult);
    *pszResult = 0;

    memset(pDr, 0, cbBuf);
    pDr->bLength = cbBuf;
    pDr->bDescriptorType = USB_STRING_DESCRIPTOR_TYPE;

    NTSTATUS Status = VBoxUsbToolGetDescriptor(pDevObj, pDr, cbBuf, USB_STRING_DESCRIPTOR_TYPE, iIndex, LangId, dwTimeoutMs);
    if (NT_SUCCESS(Status))
    {
        if (pDr->bLength >= sizeof (USB_STRING_DESCRIPTOR))
        {
            int rc = RTUtf16ToUtf8Ex(pDr->bString, (pDr->bLength - RT_OFFSETOF(USB_STRING_DESCRIPTOR, bString)) / sizeof(RTUTF16),
                                     &pszResult, cbResult, NULL /*pcch*/);
            if (RT_SUCCESS(rc))
            {
                USBLibPurgeEncoding(pszResult);
                Status = STATUS_SUCCESS;
            }
            else
                Status = STATUS_UNSUCCESSFUL;
        }
        else
            Status = STATUS_INVALID_PARAMETER;
    }
    return Status;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetLangID(PDEVICE_OBJECT pDevObj, int *pLangId, ULONG dwTimeoutMs)
{
    char aBuf[MAXIMUM_USB_STRING_LENGTH];
    AssertCompile(sizeof (aBuf) <= UINT8_MAX);
    UCHAR cbBuf = (UCHAR)sizeof (aBuf);
    PUSB_STRING_DESCRIPTOR pDr = (PUSB_STRING_DESCRIPTOR)&aBuf;

    Assert(pLangId);
    *pLangId = 0;

    memset(pDr, 0, cbBuf);
    pDr->bLength = cbBuf;
    pDr->bDescriptorType = USB_STRING_DESCRIPTOR_TYPE;

    NTSTATUS Status = VBoxUsbToolGetDescriptor(pDevObj, pDr, cbBuf, USB_STRING_DESCRIPTOR_TYPE, 0, 0, dwTimeoutMs);
    if (NT_SUCCESS(Status))
    {
        /* Just grab the first lang ID if available. In 99% cases, it will be US English (0x0409).*/
        if (pDr->bLength >= sizeof (USB_STRING_DESCRIPTOR))
        {
            AssertCompile(sizeof (pDr->bString[0]) == sizeof (uint16_t));
            *pLangId = pDr->bString[0];
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
        }
    }
    return Status;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolGetDeviceSpeed(PDEVICE_OBJECT pDevObj, BOOLEAN *pbIsHigh)
{
    Assert(pbIsHigh);
    *pbIsHigh = FALSE;

    PIRP pIrp = IoAllocateIrp(pDevObj->StackSize, FALSE);
    Assert(pIrp);
    if (!pIrp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    USB_BUS_INTERFACE_USBDI_V1 BusIf;
    PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->MajorFunction = IRP_MJ_PNP;
    pSl->MinorFunction = IRP_MN_QUERY_INTERFACE;
    pSl->Parameters.QueryInterface.InterfaceType = &USB_BUS_INTERFACE_USBDI_GUID;
    pSl->Parameters.QueryInterface.Size = sizeof (BusIf);
    pSl->Parameters.QueryInterface.Version = USB_BUSIF_USBDI_VERSION_1;
    pSl->Parameters.QueryInterface.Interface = (PINTERFACE)&BusIf;
    pSl->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    NTSTATUS Status = VBoxDrvToolIoPostSync(pDevObj, pIrp);
    Assert(NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(Status))
    {
        *pbIsHigh = BusIf.IsDeviceHighSpeed(BusIf.BusContext);
        BusIf.InterfaceDereference(BusIf.BusContext);
    }
    IoFreeIrp(pIrp);

    return Status;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolPipeClear(PDEVICE_OBJECT pDevObj, HANDLE hPipe, bool fReset)
{
    if (!hPipe)
    {
        Log(("Resetting the control pipe??\n"));
        return STATUS_SUCCESS;
    }
    USHORT u16Function = fReset ? URB_FUNCTION_RESET_PIPE : URB_FUNCTION_ABORT_PIPE;
    PURB pUrb = VBoxUsbToolUrbAlloc(u16Function, sizeof (struct _URB_PIPE_REQUEST));
    if (!pUrb)
    {
        AssertMsgFailed((__FUNCTION__": VBoxUsbToolUrbAlloc failed!\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    pUrb->UrbPipeRequest.PipeHandle = hPipe;
    pUrb->UrbPipeRequest.Reserved = 0;

    NTSTATUS Status = VBoxUsbToolUrbPost(pDevObj, pUrb, RT_INDEFINITE_WAIT);
    if (!NT_SUCCESS(Status) || !USBD_SUCCESS(pUrb->UrbHeader.Status))
    {
        AssertMsgFailed((__FUNCTION__": vboxUsbToolRequest failed with %x (%x)\n", Status, pUrb->UrbHeader.Status));
    }

    VBoxUsbToolUrbFree(pUrb);

    return Status;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolCurrentFrame(PDEVICE_OBJECT pDevObj, PIRP pIrp, PULONG piFrame)
{
    struct _URB_GET_CURRENT_FRAME_NUMBER Urb;
    Urb.Hdr.Function = URB_FUNCTION_GET_CURRENT_FRAME_NUMBER;
    Urb.Hdr.Length = sizeof(Urb);
    Urb.FrameNumber = (ULONG)-1;

    Assert(piFrame);
    *piFrame = (ULONG)-1;

    PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    pSl->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    pSl->Parameters.Others.Argument1 = (PVOID)&Urb;
    pSl->Parameters.Others.Argument2 = NULL;

    NTSTATUS Status = VBoxUsbToolUrbPost(pDevObj, (PURB)&Urb, RT_INDEFINITE_WAIT);
    Assert(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        *piFrame = Urb.FrameNumber;
    }

    return Status;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolDevUnconfigure(PDEVICE_OBJECT pDevObj)
{
    USHORT cbUrb = sizeof (struct _URB_SELECT_CONFIGURATION);
    PURB pUrb = VBoxUsbToolUrbAlloc(URB_FUNCTION_SELECT_CONFIGURATION, cbUrb);
    Assert(pUrb);
    if (!pUrb)
        return STATUS_INSUFFICIENT_RESOURCES;

    UsbBuildSelectConfigurationRequest(pUrb, (USHORT)cbUrb, NULL);

    NTSTATUS Status = VBoxUsbToolUrbPost(pDevObj, pUrb, RT_INDEFINITE_WAIT);
    Assert(NT_SUCCESS(Status));

    VBoxUsbToolUrbFree(pUrb);

    return Status;
}

VBOXUSBTOOL_DECL(PIRP) VBoxUsbToolIoBuildAsyncInternalCtl(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2)
{
    PIRP pIrp = IoAllocateIrp(pDevObj->StackSize, FALSE);
    Assert(pIrp);
    if (!pIrp)
    {
        return NULL;
    }

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = NULL;

    PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    pSl->MinorFunction = 0;
    pSl->Parameters.DeviceIoControl.IoControlCode = uCtl;
    pSl->Parameters.Others.Argument1 = pvArg1;
    pSl->Parameters.Others.Argument2 = pvArg2;
    return pIrp;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolIoInternalCtlSendSyncWithTimeout(PDEVICE_OBJECT pDevObj, ULONG uCtl,
                                                                       void *pvArg1, void *pvArg2, ULONG dwTimeoutMs)
{
    /* since we're going to cancel the irp on timeout, we should allocate our own IRP rather than using the threaded one
     * */
    PIRP pIrp = VBoxUsbToolIoBuildAsyncInternalCtl(pDevObj, uCtl, pvArg1, pvArg2);
    if (!pIrp)
    {
        WARN(("VBoxUsbToolIoBuildAsyncInternalCtl failed"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTSTATUS Status = VBoxDrvToolIoPostSyncWithTimeout(pDevObj, pIrp, dwTimeoutMs);

    IoFreeIrp(pIrp);

    return Status;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolIoInternalCtlSendAsync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2,
                                                             PKEVENT pEvent, PIO_STATUS_BLOCK pIoStatus)
{
    NTSTATUS Status;
    PIRP pIrp;
    PIO_STACK_LOCATION pSl;
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    pIrp = IoBuildDeviceIoControlRequest(uCtl, pDevObj, NULL, 0, NULL, 0, TRUE, pEvent, pIoStatus);
    if (!pIrp)
    {
        WARN(("IoBuildDeviceIoControlRequest failed!!\n"));
        pIoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
        pIoStatus->Information = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Get the next stack location as that is used for the new irp */
    pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->Parameters.Others.Argument1 = pvArg1;
    pSl->Parameters.Others.Argument2 = pvArg2;

    Status = IoCallDriver(pDevObj, pIrp);

    return Status;
}

VBOXUSBTOOL_DECL(NTSTATUS) VBoxUsbToolIoInternalCtlSendSync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2)
{
    IO_STATUS_BLOCK IoStatus = {0};
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    LOG(("Sending sync Ctl pDevObj(0x%p), uCtl(0x%x), pvArg1(0x%p), pvArg2(0x%p)", pDevObj, uCtl, pvArg1, pvArg2));

    Status = VBoxUsbToolIoInternalCtlSendAsync(pDevObj, uCtl, pvArg1, pvArg2, &Event, &IoStatus);

    if (Status == STATUS_PENDING)
    {
        LOG(("VBoxUsbToolIoInternalCtlSendAsync returned pending for pDevObj(0x%p)", pDevObj));
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
        LOG(("Pending VBoxUsbToolIoInternalCtlSendAsync completed with Status (0x%x) for pDevObj(0x%p)", Status, pDevObj));
    }
    else
    {
        LOG(("VBoxUsbToolIoInternalCtlSendAsync completed with Status (0x%x) for pDevObj(0x%p)", Status, pDevObj));
    }

    return Status;
}
