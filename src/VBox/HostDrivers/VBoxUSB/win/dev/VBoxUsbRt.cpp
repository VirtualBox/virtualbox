/* $Id: VBoxUsbRt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox USB R0 runtime
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxUsbCmn.h"
#include "../cmn/VBoxUsbIdc.h"
#include "../cmn/VBoxUsbTool.h"

#include <VBox/usblib-win.h>
#include <iprt/assert.h>
#include <VBox/log.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define _USBD_ /** @todo r=bird: What is this?? */

#define USBD_DEFAULT_PIPE_TRANSFER 0x00000008

#define VBOXUSB_MAGIC  0xABCF1423


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct VBOXUSB_URB_CONTEXT
{
    PURB pUrb;
    PMDL pMdlBuf;
    PVBOXUSBDEV_EXT pDevExt;
    PVOID pOut;
    ULONG ulTransferType;
    ULONG ulMagic;
} VBOXUSB_URB_CONTEXT, * PVBOXUSB_URB_CONTEXT;

typedef struct VBOXUSB_SETUP
{
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} VBOXUSB_SETUP, *PVBOXUSB_SETUP;



static bool vboxUsbRtCtxSetOwner(PVBOXUSBDEV_EXT pDevExt, PFILE_OBJECT pFObj)
{
    bool fRc = ASMAtomicCmpXchgPtr(&pDevExt->Rt.pOwner, pFObj, NULL);
    if (fRc)
        LogFunc(("pDevExt (0x%x) Owner(0x%x) acquired\n", pFObj));
    else
        LogFunc(("pDevExt (0x%x) Owner(0x%x) FAILED!!\n", pFObj));
    return fRc;
}

static bool vboxUsbRtCtxReleaseOwner(PVBOXUSBDEV_EXT pDevExt, PFILE_OBJECT pFObj)
{
    bool fRc = ASMAtomicCmpXchgPtr(&pDevExt->Rt.pOwner, NULL, pFObj);
    if (fRc)
        LogFunc(("pDevExt (0x%x) Owner(0x%x) released\n", pFObj));
    else
        LogFunc(("pDevExt (0x%x) Owner(0x%x) release: is NOT an owner\n", pFObj));
    return fRc;
}

static bool vboxUsbRtCtxIsOwner(PVBOXUSBDEV_EXT pDevExt, PFILE_OBJECT pFObj)
{
    PFILE_OBJECT pOwner = (PFILE_OBJECT)ASMAtomicReadPtr((void *volatile *)(&pDevExt->Rt.pOwner));
    return pOwner == pFObj;
}

static NTSTATUS vboxUsbRtIdcSubmit(ULONG uCtl, void *pvBuffer)
{
    /* we just reuse the standard usb tooling for simplicity here */
    NTSTATUS Status = VBoxUsbToolIoInternalCtlSendSync(g_VBoxUsbGlobals.RtIdc.pDevice, uCtl, pvBuffer, NULL);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

static NTSTATUS vboxUsbRtIdcInit()
{
    UNICODE_STRING UniName;
    RtlInitUnicodeString(&UniName, USBMON_DEVICE_NAME_NT);
    NTSTATUS Status = IoGetDeviceObjectPointer(&UniName, FILE_ALL_ACCESS, &g_VBoxUsbGlobals.RtIdc.pFile, &g_VBoxUsbGlobals.RtIdc.pDevice);
    if (NT_SUCCESS(Status))
    {
        VBOXUSBIDC_VERSION Version;
        vboxUsbRtIdcSubmit(VBOXUSBIDC_INTERNAL_IOCTL_GET_VERSION, &Version);
        if (NT_SUCCESS(Status))
        {
            if (   Version.u32Major == VBOXUSBIDC_VERSION_MAJOR
#if VBOXUSBIDC_VERSION_MINOR != 0
                && Version.u32Minor >= VBOXUSBIDC_VERSION_MINOR
#endif
               )
                return STATUS_SUCCESS;
            AssertFailed();
        }
        else
        {
            AssertFailed();
        }

        /* this will as well dereference the dev obj */
        ObDereferenceObject(g_VBoxUsbGlobals.RtIdc.pFile);
    }
    else
    {
        AssertFailed();
    }

    memset(&g_VBoxUsbGlobals.RtIdc, 0, sizeof (g_VBoxUsbGlobals.RtIdc));
    return Status;
}

static VOID vboxUsbRtIdcTerm()
{
    Assert(g_VBoxUsbGlobals.RtIdc.pFile);
    Assert(g_VBoxUsbGlobals.RtIdc.pDevice);
    ObDereferenceObject(g_VBoxUsbGlobals.RtIdc.pFile);
    memset(&g_VBoxUsbGlobals.RtIdc, 0, sizeof (g_VBoxUsbGlobals.RtIdc));
}

static NTSTATUS vboxUsbRtIdcReportDevStart(PDEVICE_OBJECT pPDO, HVBOXUSBIDCDEV *phDev)
{
    VBOXUSBIDC_PROXY_STARTUP Start;
    Start.u.pPDO = pPDO;

    *phDev = NULL;

    NTSTATUS Status = vboxUsbRtIdcSubmit(VBOXUSBIDC_INTERNAL_IOCTL_PROXY_STARTUP, &Start);
    Assert(Status == STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;

    *phDev = Start.u.hDev;
    return STATUS_SUCCESS;
}

static NTSTATUS vboxUsbRtIdcReportDevStop(HVBOXUSBIDCDEV hDev)
{
    VBOXUSBIDC_PROXY_TEARDOWN Stop;
    Stop.hDev = hDev;

    NTSTATUS Status = vboxUsbRtIdcSubmit(VBOXUSBIDC_INTERNAL_IOCTL_PROXY_TEARDOWN, &Stop);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}


DECLHIDDEN(NTSTATUS) vboxUsbRtGlobalsInit()
{
    return vboxUsbRtIdcInit();
}

DECLHIDDEN(VOID) vboxUsbRtGlobalsTerm()
{
    vboxUsbRtIdcTerm();
}


DECLHIDDEN(NTSTATUS) vboxUsbRtInit(PVBOXUSBDEV_EXT pDevExt)
{
    RtlZeroMemory(&pDevExt->Rt, sizeof (pDevExt->Rt));
    NTSTATUS Status = IoRegisterDeviceInterface(pDevExt->pPDO, &GUID_CLASS_VBOXUSB,
                                NULL, /* IN PUNICODE_STRING ReferenceString OPTIONAL */
                                &pDevExt->Rt.IfName);
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = vboxUsbRtIdcReportDevStart(pDevExt->pPDO, &pDevExt->Rt.hMonDev);
        Assert(Status == STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Assert(pDevExt->Rt.hMonDev);
            return STATUS_SUCCESS;
        }

        NTSTATUS tmpStatus = IoSetDeviceInterfaceState(&pDevExt->Rt.IfName, FALSE);
        Assert(tmpStatus == STATUS_SUCCESS);
        if (NT_SUCCESS(tmpStatus))
        {
            RtlFreeUnicodeString(&pDevExt->Rt.IfName);
        }
    }
    return Status;
}

/**
 * Free cached USB device/configuration descriptors
 *
 * @param   pDevExt             USB DevExt pointer
 */
static void vboxUsbRtFreeCachedDescriptors(PVBOXUSBDEV_EXT pDevExt)
{
    if (pDevExt->Rt.devdescr)
    {
        vboxUsbMemFree(pDevExt->Rt.devdescr);
        pDevExt->Rt.devdescr = NULL;
    }
    for (ULONG i = 0; i < VBOXUSBRT_MAX_CFGS; ++i)
    {
        if (pDevExt->Rt.cfgdescr[i])
        {
            vboxUsbMemFree(pDevExt->Rt.cfgdescr[i]);
            pDevExt->Rt.cfgdescr[i] = NULL;
        }
    }
}

/**
 * Free per-device interface info
 *
 * @param   pDevExt             USB DevExt pointer
 * @param   fAbortPipes         If true, also abort any open pipes
 */
static void vboxUsbRtFreeInterfaces(PVBOXUSBDEV_EXT pDevExt, BOOLEAN fAbortPipes)
{
    unsigned i;
    unsigned j;

    /*
     * Free old interface info
     */
    if (pDevExt->Rt.pVBIfaceInfo)
    {
        for (i=0;i<pDevExt->Rt.uNumInterfaces;i++)
        {
            if (pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo)
            {
                if (fAbortPipes)
                {
                    for (j=0; j<pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->NumberOfPipes; j++)
                    {
                        Log(("Aborting Pipe %d handle %x address %x\n", j,
                                 pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].PipeHandle,
                                 pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].EndpointAddress));
                        VBoxUsbToolPipeClear(pDevExt->pLowerDO, pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].PipeHandle, FALSE);
                    }
                }
                vboxUsbMemFree(pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo);
            }
            pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo = NULL;
            if (pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo)
                vboxUsbMemFree(pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo);
            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo = NULL;
        }
        vboxUsbMemFree(pDevExt->Rt.pVBIfaceInfo);
        pDevExt->Rt.pVBIfaceInfo = NULL;
    }
}

DECLHIDDEN(VOID) vboxUsbRtClear(PVBOXUSBDEV_EXT pDevExt)
{
    vboxUsbRtFreeCachedDescriptors(pDevExt);
    vboxUsbRtFreeInterfaces(pDevExt, FALSE);
}

DECLHIDDEN(NTSTATUS) vboxUsbRtRm(PVBOXUSBDEV_EXT pDevExt)
{
    if (!pDevExt->Rt.IfName.Buffer)
        return STATUS_SUCCESS;

    NTSTATUS Status = vboxUsbRtIdcReportDevStop(pDevExt->Rt.hMonDev);
    Assert(Status == STATUS_SUCCESS);
    Status = IoSetDeviceInterfaceState(&pDevExt->Rt.IfName, FALSE);
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&pDevExt->Rt.IfName);
        pDevExt->Rt.IfName.Buffer = NULL;
    }
    return Status;
}

DECLHIDDEN(NTSTATUS) vboxUsbRtStart(PVBOXUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = IoSetDeviceInterfaceState(&pDevExt->Rt.IfName, TRUE);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

static NTSTATUS vboxUsbRtCacheDescriptors(PVBOXUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
//    uint32_t uTotalLength;
//    unsigned                        i;

    /* Read device descriptor */
    Assert(!pDevExt->Rt.devdescr);
    pDevExt->Rt.devdescr = (PUSB_DEVICE_DESCRIPTOR)vboxUsbMemAlloc(sizeof (USB_DEVICE_DESCRIPTOR));
    if (pDevExt->Rt.devdescr)
    {
        memset(pDevExt->Rt.devdescr, 0, sizeof (USB_DEVICE_DESCRIPTOR));
        Status = VBoxUsbToolGetDescriptor(pDevExt->pLowerDO, pDevExt->Rt.devdescr, sizeof (USB_DEVICE_DESCRIPTOR), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, RT_INDEFINITE_WAIT);
        if (NT_SUCCESS(Status))
        {
            Assert(pDevExt->Rt.devdescr->bNumConfigurations > 0);
            PUSB_CONFIGURATION_DESCRIPTOR pDr = (PUSB_CONFIGURATION_DESCRIPTOR)vboxUsbMemAlloc(sizeof (USB_CONFIGURATION_DESCRIPTOR));
            Assert(pDr);
            if (pDr)
            {
                UCHAR i = 0;
                for (; i < pDevExt->Rt.devdescr->bNumConfigurations; ++i)
                {
                    Status = VBoxUsbToolGetDescriptor(pDevExt->pLowerDO, pDr, sizeof (USB_CONFIGURATION_DESCRIPTOR), USB_CONFIGURATION_DESCRIPTOR_TYPE, i, 0, RT_INDEFINITE_WAIT);
                    if (!NT_SUCCESS(Status))
                    {
                        break;
                    }

                    USHORT uTotalLength = pDr->wTotalLength;
                    pDevExt->Rt.cfgdescr[i] = (PUSB_CONFIGURATION_DESCRIPTOR)vboxUsbMemAlloc(uTotalLength);
                    if (!pDevExt->Rt.cfgdescr[i])
                    {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        break;
                    }

                    Status = VBoxUsbToolGetDescriptor(pDevExt->pLowerDO, pDevExt->Rt.cfgdescr[i], uTotalLength, USB_CONFIGURATION_DESCRIPTOR_TYPE, i, 0, RT_INDEFINITE_WAIT);
                    if (!NT_SUCCESS(Status))
                    {
                        break;
                    }
                }

                vboxUsbMemFree(pDr);

                if (NT_SUCCESS(Status))
                    return Status;

                /* recources will be freed in vboxUsbRtFreeCachedDescriptors below */
            }
        }

        vboxUsbRtFreeCachedDescriptors(pDevExt);
    }

    /* shoud be only on fail here */
    Assert(!NT_SUCCESS(Status));
    return Status;
}

static NTSTATUS vboxUsbRtDispatchClaimDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_CLAIMDEV pDev  = (PUSBSUP_CLAIMDEV)pIrp->AssociatedIrp.SystemBuffer;
    ULONG cbOut = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (  !pDev
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pDev)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != sizeof (*pDev))
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!vboxUsbRtCtxSetOwner(pDevExt, pFObj))
        {
            AssertFailed();
            pDev->fClaimed = false;
            cbOut = sizeof (*pDev);
            break;
        }

        vboxUsbRtFreeCachedDescriptors(pDevExt);
        Status = vboxUsbRtCacheDescriptors(pDevExt);
        if (NT_SUCCESS(Status))
        {
            pDev->fClaimed = true;
            cbOut = sizeof (*pDev);
        }
    } while (0);

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, cbOut);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtDispatchReleaseDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    NTSTATUS Status= STATUS_SUCCESS;

    if (vboxUsbRtCtxIsOwner(pDevExt, pFObj))
    {
        vboxUsbRtFreeCachedDescriptors(pDevExt);
        bool fRc = vboxUsbRtCtxReleaseOwner(pDevExt, pFObj);
        Assert(fRc); NOREF(fRc);
    }
    else
    {
        AssertFailed();
        Status = STATUS_ACCESS_DENIED;
    }

    VBoxDrvToolIoComplete(pIrp, STATUS_SUCCESS, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS vboxUsbRtGetDeviceDescription(PVBOXUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    PUSB_DEVICE_DESCRIPTOR pDr = (PUSB_DEVICE_DESCRIPTOR)vboxUsbMemAllocZ(sizeof (USB_DEVICE_DESCRIPTOR));
    if (pDr)
    {
        Status = VBoxUsbToolGetDescriptor(pDevExt->pLowerDO, pDr, sizeof(*pDr), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, RT_INDEFINITE_WAIT);
        if (NT_SUCCESS(Status))
        {
            pDevExt->Rt.idVendor    = pDr->idVendor;
            pDevExt->Rt.idProduct   = pDr->idProduct;
            pDevExt->Rt.bcdDevice   = pDr->bcdDevice;
            pDevExt->Rt.szSerial[0] = 0;

            if (pDr->iSerialNumber
#ifdef DEBUG
                || pDr->iProduct || pDr->iManufacturer
#endif
               )
            {
                int langId;
                Status = VBoxUsbToolGetLangID(pDevExt->pLowerDO, &langId, RT_INDEFINITE_WAIT);
                if (NT_SUCCESS(Status))
                {
                    Status = VBoxUsbToolGetStringDescriptor(pDevExt->pLowerDO, pDevExt->Rt.szSerial, sizeof (pDevExt->Rt.szSerial),
                                                            pDr->iSerialNumber, langId, RT_INDEFINITE_WAIT);
                }
                else
                {
                    Status = STATUS_SUCCESS;
                }
            }
        }
        vboxUsbMemFree(pDr);
    }

    return Status;
}

static NTSTATUS vboxUsbRtDispatchGetDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PUSBSUP_GETDEV pDev = (PUSBSUP_GETDEV)pIrp->AssociatedIrp.SystemBuffer;
    ULONG cbOut = 0;

    /* don't check for owner since this request is allowed for non-owners as well */
    NTSTATUS Status;
    if (   pDev
        && pSl->Parameters.DeviceIoControl.InputBufferLength  == sizeof(*pDev)
        && pSl->Parameters.DeviceIoControl.OutputBufferLength == sizeof(*pDev))
    {
        /* Even if we don't return it, we need to query the HS flag for later use. */
        Status = VBoxUsbToolGetDeviceSpeed(pDevExt->pLowerDO, &pDevExt->Rt.fIsHighSpeed);
        if (NT_SUCCESS(Status))
        {
            pDev->hDevice = pDevExt->Rt.hMonDev;
            cbOut = sizeof (*pDev);
        }
    }
    else
        Status = STATUS_INVALID_PARAMETER;

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, cbOut);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtDispatchUsbReset(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION  pSl   = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT        pFObj = pSl->FileObject;
    NTSTATUS            rcNt;
    if (pFObj)
    {
        if (vboxUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            if (   pIrp->AssociatedIrp.SystemBuffer == NULL
                && pSl->Parameters.DeviceIoControl.InputBufferLength == 0
                && pSl->Parameters.DeviceIoControl.OutputBufferLength == 0)
            {
                rcNt = VBoxUsbToolIoInternalCtlSendSync(pDevExt->pLowerDO, IOCTL_INTERNAL_USB_RESET_PORT, NULL, NULL);
                Assert(NT_SUCCESS(rcNt));
            }
            else
            {
                AssertFailed();
                rcNt = STATUS_INVALID_PARAMETER;
            }
        }
        else
        {
            AssertFailed();
            rcNt = STATUS_ACCESS_DENIED;
        }
    }
    else
    {
        AssertFailed();
        rcNt = STATUS_INVALID_PARAMETER;
    }

    Assert(rcNt != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, rcNt, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return rcNt;
}

static PUSB_CONFIGURATION_DESCRIPTOR vboxUsbRtFindConfigDesc(PVBOXUSBDEV_EXT pDevExt, uint8_t uConfiguration)
{
    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = NULL;

    for (ULONG i = 0; i < VBOXUSBRT_MAX_CFGS; ++i)
    {
        if (pDevExt->Rt.cfgdescr[i])
        {
            if (pDevExt->Rt.cfgdescr[i]->bConfigurationValue == uConfiguration)
            {
                pCfgDr = pDevExt->Rt.cfgdescr[i];
                break;
            }
        }
    }

    return pCfgDr;
}

static NTSTATUS vboxUsbRtSetConfig(PVBOXUSBDEV_EXT pDevExt, uint8_t uConfiguration)
{
    PURB pUrb = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t i;

    if (!uConfiguration)
    {
        pUrb = VBoxUsbToolUrbAllocZ(URB_FUNCTION_SELECT_CONFIGURATION, sizeof (struct _URB_SELECT_CONFIGURATION));
        if (!pUrb)
        {
            AssertMsgFailed((__FUNCTION__": VBoxUsbToolUrbAlloc failed\n"));
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        vboxUsbRtFreeInterfaces(pDevExt, TRUE);

        pUrb->UrbSelectConfiguration.ConfigurationDescriptor = NULL;

        Status = VBoxUsbToolUrbPost(pDevExt->pLowerDO, pUrb, RT_INDEFINITE_WAIT);
        if (NT_SUCCESS(Status) && USBD_SUCCESS(pUrb->UrbHeader.Status))
        {
            pDevExt->Rt.hConfiguration = pUrb->UrbSelectConfiguration.ConfigurationHandle;
            pDevExt->Rt.uConfigValue = uConfiguration;
        }
        else
        {
            AssertMsgFailed((__FUNCTION__": VBoxUsbToolUrbPost failed Status (0x%x), usb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
        }

        VBoxUsbToolUrbFree(pUrb);

        return Status;
    }

/** @todo r=bird: Need to write a script for fixing these kind of clueless use
 *        of AssertMsgFailed (into AssertMsgReturn).  The __FUNCTION__ is just
 *        the topping it off - the assertion message includes function, file and
 *        line number. Duh! */
    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = vboxUsbRtFindConfigDesc(pDevExt, uConfiguration);
    if (!pCfgDr)
    {
        AssertMsgFailed((__FUNCTION__": VBoxUSBFindConfigDesc did not find cfg (%d)\n", uConfiguration));
        return STATUS_INVALID_PARAMETER;
    }

    PUSBD_INTERFACE_LIST_ENTRY pIfLe = (PUSBD_INTERFACE_LIST_ENTRY)vboxUsbMemAllocZ((pCfgDr->bNumInterfaces + 1) * sizeof(USBD_INTERFACE_LIST_ENTRY));
    if (!pIfLe)
    {
        AssertMsgFailed((__FUNCTION__": vboxUsbMemAllocZ for pIfLe failed\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < pCfgDr->bNumInterfaces; i++)
    {
        pIfLe[i].InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(pCfgDr, pCfgDr, i, 0, -1, -1, -1);
        if (!pIfLe[i].InterfaceDescriptor)
        {
            AssertMsgFailed((__FUNCTION__": interface %d not found\n", i));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
    }
    pIfLe[pCfgDr->bNumInterfaces].InterfaceDescriptor = NULL;

    if (NT_SUCCESS(Status))
    {
        pUrb = USBD_CreateConfigurationRequestEx(pCfgDr, pIfLe);
        if (pUrb)
        {
            Status = VBoxUsbToolUrbPost(pDevExt->pLowerDO, pUrb, RT_INDEFINITE_WAIT);
            if (NT_SUCCESS(Status) && USBD_SUCCESS(pUrb->UrbHeader.Status))
            {
                vboxUsbRtFreeInterfaces(pDevExt, FALSE);

                pDevExt->Rt.hConfiguration = pUrb->UrbSelectConfiguration.ConfigurationHandle;
                pDevExt->Rt.uConfigValue = uConfiguration;
                pDevExt->Rt.uNumInterfaces = pCfgDr->bNumInterfaces;

                pDevExt->Rt.pVBIfaceInfo = (VBOXUSB_IFACE_INFO*)vboxUsbMemAllocZ(pDevExt->Rt.uNumInterfaces * sizeof (VBOXUSB_IFACE_INFO));
                if (pDevExt->Rt.pVBIfaceInfo)
                {
                    Assert(NT_SUCCESS(Status));
                    for (i = 0; i < pDevExt->Rt.uNumInterfaces; i++)
                    {
                        size_t uTotalIfaceInfoLength = GET_USBD_INTERFACE_SIZE(pIfLe[i].Interface->NumberOfPipes);
                        pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo = (PUSBD_INTERFACE_INFORMATION)vboxUsbMemAlloc(uTotalIfaceInfoLength);
                        if (!pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo)
                        {
                            AssertMsgFailed((__FUNCTION__": vboxUsbMemAlloc failed\n"));
                            Status = STATUS_INSUFFICIENT_RESOURCES;
                            break;
                        }

                        if (pIfLe[i].Interface->NumberOfPipes > 0)
                        {
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo = (VBOXUSB_PIPE_INFO *)vboxUsbMemAlloc(pIfLe[i].Interface->NumberOfPipes * sizeof(VBOXUSB_PIPE_INFO));
                            if (!pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo)
                            {
                                AssertMsgFailed((__FUNCTION__": vboxUsbMemAlloc failed\n"));
                                Status = STATUS_NO_MEMORY;
                                break;
                            }
                        }
                        else
                        {
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo = NULL;
                        }

                        RtlCopyMemory(pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo, pIfLe[i].Interface, uTotalIfaceInfoLength);

                        for (ULONG j = 0; j < pIfLe[i].Interface->NumberOfPipes; j++)
                        {
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j].EndpointAddress = pIfLe[i].Interface->Pipes[j].EndpointAddress;
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j].NextScheduledFrame = 0;
                        }
                    }

//                    if (NT_SUCCESS(Status))
//                    {
//
//                    }
                }
                else
                {
                    AssertMsgFailed((__FUNCTION__": vboxUsbMemAllocZ failed\n"));
                    Status = STATUS_NO_MEMORY;
                }
            }
            else
            {
                AssertMsgFailed((__FUNCTION__": VBoxUsbToolUrbPost failed Status (0x%x), usb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
            }
            ExFreePool(pUrb);
        }
        else
        {
            AssertMsgFailed((__FUNCTION__": USBD_CreateConfigurationRequestEx failed\n"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    vboxUsbMemFree(pIfLe);

    return Status;
}

static NTSTATUS vboxUsbRtDispatchUsbSetConfig(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_SET_CONFIG pCfg  = (PUSBSUP_SET_CONFIG)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!vboxUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (      !pCfg
                || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pCfg)
                || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = vboxUsbRtSetConfig(pDevExt, pCfg->bConfigurationValue);
    } while (0);

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtSetInterface(PVBOXUSBDEV_EXT pDevExt, uint32_t InterfaceNumber, int AlternateSetting)
{
    AssertMsgReturn(pDevExt->Rt.uConfigValue, ("Can't select an interface without an active configuration\n"),
                    STATUS_INVALID_PARAMETER);
    AssertMsgReturn(InterfaceNumber < pDevExt->Rt.uNumInterfaces, ("InterfaceNumber %d too high!!\n", InterfaceNumber),
                    STATUS_INVALID_PARAMETER);
    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = vboxUsbRtFindConfigDesc(pDevExt, pDevExt->Rt.uConfigValue);
    AssertMsgReturn(pCfgDr, ("configuration %d not found!!\n", pDevExt->Rt.uConfigValue),
                    STATUS_INVALID_PARAMETER);
    PUSB_INTERFACE_DESCRIPTOR pIfDr = USBD_ParseConfigurationDescriptorEx(pCfgDr, pCfgDr, InterfaceNumber, AlternateSetting, -1, -1, -1);
    AssertMsgReturn(pIfDr, ("invalid interface %d or alternate setting %d\n", InterfaceNumber, AlternateSetting),
                    STATUS_UNSUCCESSFUL);

    USHORT uUrbSize = GET_SELECT_INTERFACE_REQUEST_SIZE(pIfDr->bNumEndpoints);
    ULONG uTotalIfaceInfoLength = GET_USBD_INTERFACE_SIZE(pIfDr->bNumEndpoints);
    NTSTATUS Status = STATUS_SUCCESS;
    PURB pUrb;
    PUSBD_INTERFACE_INFORMATION pNewIFInfo = NULL;
    VBOXUSB_PIPE_INFO *pNewPipeInfo = NULL;

    if (pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo)
    {
        /* Clear pipes associated with the interface, else Windows may hang. */
        for (ULONG i = 0; i < pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo->NumberOfPipes; i++)
            VBoxUsbToolPipeClear(pDevExt->pLowerDO, pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo->Pipes[i].PipeHandle, FALSE);
    }

    do {
        /* First allocate all the structures we'll need. */
        pUrb = VBoxUsbToolUrbAllocZ(0, uUrbSize);
        if (!pUrb)
        {
            AssertMsgFailed((__FUNCTION__": VBoxUsbToolUrbAllocZ failed\n"));
            Status = STATUS_NO_MEMORY;
            break;
        }

        pNewIFInfo = (PUSBD_INTERFACE_INFORMATION)vboxUsbMemAlloc(uTotalIfaceInfoLength);
        if (!pNewIFInfo)
        {
            AssertMsgFailed((__FUNCTION__": Failed allocating interface storage\n"));
            Status = STATUS_NO_MEMORY;
            break;
        }

        if (pIfDr->bNumEndpoints > 0)
        {
            pNewPipeInfo = (VBOXUSB_PIPE_INFO *)vboxUsbMemAlloc(pIfDr->bNumEndpoints * sizeof(VBOXUSB_PIPE_INFO));
            if (!pNewPipeInfo)
            {
                AssertMsgFailed((__FUNCTION__": Failed allocating pipe info storage\n"));
                Status = STATUS_NO_MEMORY;
                break;
            }
        }
        else
            pNewPipeInfo = NULL;

        /* Now that we have all the bits, select the interface. */
        UsbBuildSelectInterfaceRequest(pUrb, uUrbSize, pDevExt->Rt.hConfiguration, InterfaceNumber, AlternateSetting);
        pUrb->UrbSelectInterface.Interface.Length = GET_USBD_INTERFACE_SIZE(pIfDr->bNumEndpoints);

        Status = VBoxUsbToolUrbPost(pDevExt->pLowerDO, pUrb, RT_INDEFINITE_WAIT);
        if (NT_SUCCESS(Status) && USBD_SUCCESS(pUrb->UrbHeader.Status))
        {
            /* Free the old memory and put new in. */
            if (pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo)
                vboxUsbMemFree(pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo);
            pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo = pNewIFInfo;
            if (pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo)
                vboxUsbMemFree(pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo);
            pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo = pNewPipeInfo;
            pNewPipeInfo = NULL; pNewIFInfo = NULL; /* Don't try to free it again. */

            USBD_INTERFACE_INFORMATION *pIfInfo = &pUrb->UrbSelectInterface.Interface;
            memcpy(pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo, pIfInfo, GET_USBD_INTERFACE_SIZE(pIfDr->bNumEndpoints));

            Assert(pIfInfo->NumberOfPipes == pIfDr->bNumEndpoints);
            for (ULONG i = 0; i < pIfInfo->NumberOfPipes; i++)
            {
                pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo[i].EndpointAddress = pIfInfo->Pipes[i].EndpointAddress;
                pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo[i].NextScheduledFrame = 0;
            }
        }
        else
        {
            AssertMsgFailed((__FUNCTION__": VBoxUsbToolUrbPost failed Status (0x%x) usb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
        }
    } while (0);

    /* Clean up. */
    if (pUrb)
        VBoxUsbToolUrbFree(pUrb);
    if (pNewIFInfo)
        vboxUsbMemFree(pNewIFInfo);
    if (pNewPipeInfo)
        vboxUsbMemFree(pNewPipeInfo);

    return Status;
}

static NTSTATUS vboxUsbRtDispatchUsbSelectInterface(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_SELECT_INTERFACE pIf = (PUSBSUP_SELECT_INTERFACE)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!vboxUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (  !pIf
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pIf)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = vboxUsbRtSetInterface(pDevExt, pIf->bInterfaceNumber, pIf->bAlternateSetting);
    } while (0);

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static HANDLE vboxUsbRtGetPipeHandle(PVBOXUSBDEV_EXT pDevExt, uint32_t EndPointAddress)
{
    if (EndPointAddress == 0)
        return pDevExt->Rt.hPipe0;

    for (ULONG i = 0; i < pDevExt->Rt.uNumInterfaces; i++)
    {
        for (ULONG j = 0; j < pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->NumberOfPipes; j++)
        {
            /* Note that bit 7 determines pipe direction, but is still significant
             * because endpoints may be numbered like 0x01, 0x81, 0x02, 0x82 etc.
             */
            if (pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].EndpointAddress == EndPointAddress)
                return pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].PipeHandle;
        }
    }
    return 0;
}

static VBOXUSB_PIPE_INFO* vboxUsbRtGetPipeInfo(PVBOXUSBDEV_EXT pDevExt, uint32_t EndPointAddress)
{
    for (ULONG i = 0; i < pDevExt->Rt.uNumInterfaces; i++)
    {
        for (ULONG j = 0; j < pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->NumberOfPipes; j++)
        {
            if (pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j].EndpointAddress == EndPointAddress)
                return &pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j];
        }
    }
    return NULL;
}



static NTSTATUS vboxUsbRtClearEndpoint(PVBOXUSBDEV_EXT pDevExt, uint32_t EndPointAddress, bool fReset)
{
    NTSTATUS Status = VBoxUsbToolPipeClear(pDevExt->pLowerDO, vboxUsbRtGetPipeHandle(pDevExt, EndPointAddress), fReset);
    if (!NT_SUCCESS(Status))
    {
        AssertMsgFailed((__FUNCTION__": VBoxUsbToolPipeClear failed Status (0x%x)\n", Status));
    }

    return Status;
}

static NTSTATUS vboxUsbRtDispatchUsbClearEndpoint(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_CLEAR_ENDPOINT pCe = (PUSBSUP_CLEAR_ENDPOINT)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!vboxUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (   !pCe
             || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pCe)
             || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = vboxUsbRtClearEndpoint(pDevExt, pCe->bEndpoint, TRUE);
    } while (0);

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtDispatchUsbAbortEndpoint(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_CLEAR_ENDPOINT pCe = (PUSBSUP_CLEAR_ENDPOINT)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!vboxUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (  !pCe
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pCe)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = vboxUsbRtClearEndpoint(pDevExt, pCe->bEndpoint, FALSE);
    } while (0);

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtUrbSendCompletion(PDEVICE_OBJECT pDevObj, IRP *pIrp, void *pvContext)
{
    RT_NOREF1(pDevObj);

    if (!pvContext)
    {
        AssertMsgFailed((__FUNCTION__":  context is NULL\n"));
        pIrp->IoStatus.Information = 0;
        return STATUS_CONTINUE_COMPLETION;
    }

    PVBOXUSB_URB_CONTEXT pContext = (PVBOXUSB_URB_CONTEXT)pvContext;

    if (pContext->ulMagic != VBOXUSB_MAGIC)
    {
        AssertMsgFailed((__FUNCTION__": Invalid context magic\n"));
        pIrp->IoStatus.Information = 0;
        return STATUS_CONTINUE_COMPLETION;
    }

    PURB pUrb = pContext->pUrb;
    PMDL pMdlBuf = pContext->pMdlBuf;
    PUSBSUP_URB pUrbInfo = (PUSBSUP_URB)pContext->pOut;
    PVBOXUSBDEV_EXT pDevExt = pContext->pDevExt;

    if (!pUrb || !pMdlBuf || !pUrbInfo || !pDevExt)
    {
        AssertMsgFailed((__FUNCTION__": Invalid args\n"));
        if (pDevExt)
            vboxUsbDdiStateRelease(pDevExt);
        pIrp->IoStatus.Information = 0;
        return STATUS_CONTINUE_COMPLETION;
    }

    NTSTATUS Status = pIrp->IoStatus.Status;
    if (Status == STATUS_SUCCESS)
    {
        switch(pUrb->UrbHeader.Status)
        {
            case USBD_STATUS_CRC:
                pUrbInfo->error = USBSUP_XFER_CRC;
                break;
            case USBD_STATUS_SUCCESS:
                pUrbInfo->error = USBSUP_XFER_OK;
                break;
            case USBD_STATUS_STALL_PID:
                pUrbInfo->error = USBSUP_XFER_STALL;
                break;
            case USBD_STATUS_INVALID_URB_FUNCTION:
            case USBD_STATUS_INVALID_PARAMETER:
                AssertMsgFailed((__FUNCTION__": sw error, urb Status (0x%x)\n", pUrb->UrbHeader.Status));
            case USBD_STATUS_DEV_NOT_RESPONDING:
            default:
                pUrbInfo->error = USBSUP_XFER_DNR;
                break;
        }

        switch(pContext->ulTransferType)
        {
            case USBSUP_TRANSFER_TYPE_MSG:
                pUrbInfo->len = pUrb->UrbControlTransfer.TransferBufferLength;
                /* QUSB_TRANSFER_TYPE_MSG is a control transfer, but it is special
                 * the first 8 bytes of the buffer is the setup packet so the real
                 * data length is therefore urb->len - 8
                 */
                pUrbInfo->len += sizeof (pUrb->UrbControlTransfer.SetupPacket);

                /* If a control URB was successfully completed on the default control
                 * pipe, stash away the handle. When submitting the URB, we don't need
                 * to know (and initially don't have) the handle. If we want to abort
                 * the default control pipe, we *have* to have a handle. This is how we
                 * find out what the handle is.
                 */
                if (!pUrbInfo->ep && (pDevExt->Rt.hPipe0 == NULL))
                {
                    pDevExt->Rt.hPipe0 = pUrb->UrbControlTransfer.PipeHandle;
                }

                break;
            case USBSUP_TRANSFER_TYPE_ISOC:
                pUrbInfo->len = pUrb->UrbIsochronousTransfer.TransferBufferLength;
                break;
            case USBSUP_TRANSFER_TYPE_BULK:
            case USBSUP_TRANSFER_TYPE_INTR:
                if (pUrbInfo->dir == USBSUP_DIRECTION_IN && pUrbInfo->error == USBSUP_XFER_OK
                        && !(pUrbInfo->flags & USBSUP_FLAG_SHORT_OK)
                        && pUrbInfo->len > pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength
                        )
                {
                    /* If we don't use the USBD_SHORT_TRANSFER_OK flag, the returned buffer lengths are
                     * wrong for short transfers (always a multiple of max packet size?). So we just figure
                     * out if this was a data underrun on our own.
                     */
                    pUrbInfo->error = USBSUP_XFER_UNDERRUN;
                }
                pUrbInfo->len = pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
                break;
            default:
                break;
        }
    }
    else
    {
        pUrbInfo->len = 0;

        LogFunc(("URB failed Status (0x%x) urb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
#ifdef DEBUG
        switch(pContext->ulTransferType)
        {
            case USBSUP_TRANSFER_TYPE_MSG:
                LogRel(("Msg (CTRL) length=%d\n", pUrb->UrbControlTransfer.TransferBufferLength));
                break;
            case USBSUP_TRANSFER_TYPE_ISOC:
                LogRel(("ISOC length=%d\n", pUrb->UrbIsochronousTransfer.TransferBufferLength));
                break;
            case USBSUP_TRANSFER_TYPE_BULK:
            case USBSUP_TRANSFER_TYPE_INTR:
                LogRel(("BULK/INTR length=%d\n", pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength));
                break;
        }
#endif
        switch(pUrb->UrbHeader.Status)
        {
            case USBD_STATUS_CRC:
                pUrbInfo->error = USBSUP_XFER_CRC;
                Status = STATUS_SUCCESS;
                break;
            case USBD_STATUS_STALL_PID:
                pUrbInfo->error = USBSUP_XFER_STALL;
                Status = STATUS_SUCCESS;
                break;
            case USBD_STATUS_DEV_NOT_RESPONDING:
            case USBD_STATUS_DEVICE_GONE:
                pUrbInfo->error = USBSUP_XFER_DNR;
                Status = STATUS_SUCCESS;
                break;
            case ((USBD_STATUS)0xC0010000L): // USBD_STATUS_CANCELED - too bad usbdi.h and usb.h aren't consistent!
                /// @todo What the heck are we really supposed to do here?
                pUrbInfo->error = USBSUP_XFER_STALL;
                Status = STATUS_SUCCESS;
                break;
            case USBD_STATUS_BAD_START_FRAME:   // This one really shouldn't happen
            case USBD_STATUS_ISOCH_REQUEST_FAILED:
                pUrbInfo->error = USBSUP_XFER_NAC;
                Status = STATUS_SUCCESS;
                break;
            default:
                AssertMsgFailed((__FUNCTION__": err Status (0x%x) (0x%x)\n", Status, pUrb->UrbHeader.Status));
                pUrbInfo->error = USBSUP_XFER_DNR;
                Status = STATUS_SUCCESS;
                break;
        }
    }
    // For isochronous transfers, always update the individual packets
    if (pContext->ulTransferType == USBSUP_TRANSFER_TYPE_ISOC)
    {
        Assert(pUrbInfo->numIsoPkts == pUrb->UrbIsochronousTransfer.NumberOfPackets);
        for (ULONG i = 0; i < pUrbInfo->numIsoPkts; ++i)
        {
            Assert(pUrbInfo->aIsoPkts[i].off == pUrb->UrbIsochronousTransfer.IsoPacket[i].Offset);
            pUrbInfo->aIsoPkts[i].cb = (uint16_t)pUrb->UrbIsochronousTransfer.IsoPacket[i].Length;
            switch (pUrb->UrbIsochronousTransfer.IsoPacket[i].Status)
            {
                case USBD_STATUS_SUCCESS:
                    pUrbInfo->aIsoPkts[i].stat = USBSUP_XFER_OK;
                    break;
                case USBD_STATUS_NOT_ACCESSED:
                    pUrbInfo->aIsoPkts[i].stat = USBSUP_XFER_NAC;
                    break;
                default:
                    pUrbInfo->aIsoPkts[i].stat = USBSUP_XFER_STALL;
                    break;
            }
        }
    }

    MmUnlockPages(pMdlBuf);
    IoFreeMdl(pMdlBuf);

    vboxUsbMemFree(pContext);

    vboxUsbDdiStateRelease(pDevExt);

    Assert(pIrp->IoStatus.Status != STATUS_IO_TIMEOUT);
    pIrp->IoStatus.Information = sizeof(*pUrbInfo);
    pIrp->IoStatus.Status = Status;
    return STATUS_CONTINUE_COMPLETION;
}

static NTSTATUS vboxUsbRtUrbSend(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp, PUSBSUP_URB pUrbInfo)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXUSB_URB_CONTEXT pContext = NULL;
    PMDL pMdlBuf = NULL;
    ULONG cbUrb;

    Assert(pUrbInfo);
    if (pUrbInfo->type == USBSUP_TRANSFER_TYPE_ISOC)
    {
        Assert(pUrbInfo->numIsoPkts <= 8);
        cbUrb = GET_ISO_URB_SIZE(pUrbInfo->numIsoPkts);
    }
    else
        cbUrb = sizeof (URB);

    do
    {
        pContext = (PVBOXUSB_URB_CONTEXT)vboxUsbMemAllocZ(cbUrb + sizeof (VBOXUSB_URB_CONTEXT));
        if (!pContext)
        {
            AssertMsgFailed((__FUNCTION__": vboxUsbMemAlloc failed\n"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        PURB pUrb = (PURB)(pContext + 1);
        HANDLE hPipe = NULL;
        if (pUrbInfo->ep)
        {
            hPipe = vboxUsbRtGetPipeHandle(pDevExt, pUrbInfo->ep | ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? 0x80 : 0x00));
            if (!hPipe)
            {
                AssertMsgFailed((__FUNCTION__": vboxUsbRtGetPipeHandle failed for endpoint (0x%x)\n", pUrbInfo->ep));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        pMdlBuf = IoAllocateMdl(pUrbInfo->buf, (ULONG)pUrbInfo->len, FALSE, FALSE, NULL);
        if (!pMdlBuf)
        {
            AssertMsgFailed((__FUNCTION__": IoAllocateMdl failed for buffer (0x%p) length (%d)\n", pUrbInfo->buf, pUrbInfo->len));
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        __try
        {
            MmProbeAndLockPages(pMdlBuf, KernelMode, IoModifyAccess);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = GetExceptionCode();
            IoFreeMdl(pMdlBuf);
            pMdlBuf = NULL;
            AssertMsgFailed((__FUNCTION__": Exception Code (0x%x)\n", Status));
            break;
        }

        /* For some reason, passing a MDL in the URB does not work reliably. Notably
         * the iPhone when used with iTunes fails.
         */
        PVOID pBuffer = MmGetSystemAddressForMdlSafe(pMdlBuf, NormalPagePriority);
        if (!pBuffer)
        {
            AssertMsgFailed((__FUNCTION__": MmGetSystemAddressForMdlSafe failed\n"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        switch (pUrbInfo->type)
        {
            case USBSUP_TRANSFER_TYPE_MSG:
            {
                pUrb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
                pUrb->UrbHeader.Length = sizeof (struct _URB_CONTROL_TRANSFER);
                pUrb->UrbControlTransfer.PipeHandle = hPipe;
                pUrb->UrbControlTransfer.TransferBufferLength = (ULONG)pUrbInfo->len;
                pUrb->UrbControlTransfer.TransferFlags = ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);
                pUrb->UrbControlTransfer.UrbLink = 0;

                if (!hPipe)
                    pUrb->UrbControlTransfer.TransferFlags |= USBD_DEFAULT_PIPE_TRANSFER;

                /* QUSB_TRANSFER_TYPE_MSG is a control transfer, but it is special
                 * the first 8 bytes of the buffer is the setup packet so the real
                 * data length is therefore pUrb->len - 8
                 */
                //PVBOXUSB_SETUP pSetup = (PVBOXUSB_SETUP)pUrb->UrbControlTransfer.SetupPacket;
                memcpy(pUrb->UrbControlTransfer.SetupPacket, pBuffer, min(sizeof (pUrb->UrbControlTransfer.SetupPacket), pUrbInfo->len));

                if (pUrb->UrbControlTransfer.TransferBufferLength <= sizeof (pUrb->UrbControlTransfer.SetupPacket))
                    pUrb->UrbControlTransfer.TransferBufferLength = 0;
                else
                    pUrb->UrbControlTransfer.TransferBufferLength -= sizeof (pUrb->UrbControlTransfer.SetupPacket);

                pUrb->UrbControlTransfer.TransferBuffer = (uint8_t *)pBuffer + sizeof(pUrb->UrbControlTransfer.SetupPacket);
                pUrb->UrbControlTransfer.TransferBufferMDL = 0;
                pUrb->UrbControlTransfer.TransferFlags |= USBD_SHORT_TRANSFER_OK;
                break;
            }
            case USBSUP_TRANSFER_TYPE_ISOC:
            {
                Assert(hPipe);
                VBOXUSB_PIPE_INFO *pPipeInfo = vboxUsbRtGetPipeInfo(pDevExt, pUrbInfo->ep | ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? 0x80 : 0x00));
                if (pPipeInfo == NULL)
                {
                    /* Can happen if the isoc request comes in too early or late. */
                    AssertMsgFailed((__FUNCTION__": pPipeInfo not found\n"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                pUrb->UrbHeader.Function = URB_FUNCTION_ISOCH_TRANSFER;
                pUrb->UrbHeader.Length = (USHORT)cbUrb;
                pUrb->UrbIsochronousTransfer.PipeHandle = hPipe;
                pUrb->UrbIsochronousTransfer.TransferBufferLength = (ULONG)pUrbInfo->len;
                pUrb->UrbIsochronousTransfer.TransferBufferMDL = 0;
                pUrb->UrbIsochronousTransfer.TransferBuffer = pBuffer;
                pUrb->UrbIsochronousTransfer.TransferFlags = ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);
                pUrb->UrbIsochronousTransfer.TransferFlags |= USBD_SHORT_TRANSFER_OK;  // May be implied already
                pUrb->UrbIsochronousTransfer.NumberOfPackets = pUrbInfo->numIsoPkts;
                pUrb->UrbIsochronousTransfer.ErrorCount = 0;
                pUrb->UrbIsochronousTransfer.UrbLink = 0;

                Assert(pUrbInfo->numIsoPkts == pUrb->UrbIsochronousTransfer.NumberOfPackets);
                for (ULONG i = 0; i < pUrbInfo->numIsoPkts; ++i)
                {
                    pUrb->UrbIsochronousTransfer.IsoPacket[i].Offset = pUrbInfo->aIsoPkts[i].off;
                    pUrb->UrbIsochronousTransfer.IsoPacket[i].Length = pUrbInfo->aIsoPkts[i].cb;
                }

                /* We have to schedule the URBs ourselves. There is an ASAP flag but
                 * that can only be reliably used after pipe creation/reset, ie. it's
                 * almost completely useless.
                 */
                ULONG iFrame, iStartFrame;
                VBoxUsbToolCurrentFrame(pDevExt->pLowerDO, pIrp, &iFrame);
                iFrame += 2;
                iStartFrame = pPipeInfo->NextScheduledFrame;
                if ((iFrame < iStartFrame) || (iStartFrame > iFrame + 512))
                    iFrame = iStartFrame;
                /* For full-speed devices, there must be one transfer per frame (Windows USB
                 * stack requirement), but URBs can contain multiple packets. For high-speed or
                 * faster transfers, we expect one URB per frame, regardless of the interval.
                 */
                if (pDevExt->Rt.devdescr->bcdUSB < 0x300 && !pDevExt->Rt.fIsHighSpeed)
                    pPipeInfo->NextScheduledFrame = iFrame + pUrbInfo->numIsoPkts;
                else
                    pPipeInfo->NextScheduledFrame = iFrame + 1;
                pUrb->UrbIsochronousTransfer.StartFrame = iFrame;
                break;
            }
            case USBSUP_TRANSFER_TYPE_BULK:
            case USBSUP_TRANSFER_TYPE_INTR:
            {
                Assert(pUrbInfo->dir != USBSUP_DIRECTION_SETUP);
                Assert(pUrbInfo->dir == USBSUP_DIRECTION_IN || pUrbInfo->type == USBSUP_TRANSFER_TYPE_BULK);
                Assert(hPipe);

                pUrb->UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
                pUrb->UrbHeader.Length = sizeof (struct _URB_BULK_OR_INTERRUPT_TRANSFER);
                pUrb->UrbBulkOrInterruptTransfer.PipeHandle = hPipe;
                pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength = (ULONG)pUrbInfo->len;
                pUrb->UrbBulkOrInterruptTransfer.TransferBufferMDL = 0;
                pUrb->UrbBulkOrInterruptTransfer.TransferBuffer = pBuffer;
                pUrb->UrbBulkOrInterruptTransfer.TransferFlags = ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);

                if (pUrb->UrbBulkOrInterruptTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)
                    pUrb->UrbBulkOrInterruptTransfer.TransferFlags |= (USBD_SHORT_TRANSFER_OK);

                pUrb->UrbBulkOrInterruptTransfer.UrbLink = 0;
                break;
            }
            default:
            {
                AssertFailed();
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        if (!NT_SUCCESS(Status))
        {
            break;
        }

        pContext->pDevExt = pDevExt;
        pContext->pMdlBuf = pMdlBuf;
        pContext->pUrb = pUrb;
        pContext->pOut = pUrbInfo;
        pContext->ulTransferType = pUrbInfo->type;
        pContext->ulMagic = VBOXUSB_MAGIC;

        PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
        pSl->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        pSl->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
        pSl->Parameters.Others.Argument1 = pUrb;
        pSl->Parameters.Others.Argument2 = NULL;

        IoSetCompletionRoutine(pIrp, vboxUsbRtUrbSendCompletion, pContext, TRUE, TRUE, TRUE);
        IoMarkIrpPending(pIrp);
        Status = IoCallDriver(pDevExt->pLowerDO, pIrp);
        AssertMsg(NT_SUCCESS(Status), (__FUNCTION__": IoCallDriver failed Status (0x%x)\n", Status));
        return STATUS_PENDING;
    } while (0);

    Assert(!NT_SUCCESS(Status));

    if (pMdlBuf)
    {
        if (pMdlBuf->MdlFlags & MDL_PAGES_LOCKED)
            MmUnlockPages(pMdlBuf);

        IoFreeMdl(pMdlBuf);
    }

    if (pContext)
        vboxUsbMemFree(pContext);

    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtDispatchSendUrb(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_URB pUrbInfo = (PUSBSUP_URB)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!vboxUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (  !pUrbInfo
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pUrbInfo)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != sizeof (*pUrbInfo))
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        return vboxUsbRtUrbSend(pDevExt, pIrp, pUrbInfo);
    } while (0);

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtDispatchIsOperational(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    VBoxDrvToolIoComplete(pIrp, STATUS_SUCCESS, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS vboxUsbRtDispatchGetVersion(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PUSBSUP_VERSION pVer= (PUSBSUP_VERSION)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;

    if (   pVer
        && pSl->Parameters.DeviceIoControl.InputBufferLength == 0
        && pSl->Parameters.DeviceIoControl.OutputBufferLength == sizeof(*pVer))
    {
        pVer->u32Major = USBDRV_MAJOR_VERSION;
        pVer->u32Minor = USBDRV_MINOR_VERSION;
    }
    else
    {
        AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
        Status = STATUS_INVALID_PARAMETER;
    }

    Assert(Status != STATUS_PENDING);
    VBoxDrvToolIoComplete(pIrp, Status, sizeof (*pVer));
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbRtDispatchDefault(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    VBoxDrvToolIoComplete(pIrp, STATUS_INVALID_DEVICE_REQUEST, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return STATUS_INVALID_DEVICE_REQUEST;
}

DECLHIDDEN(NTSTATUS) vboxUsbRtCreate(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    RT_NOREF1(pDevExt);
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    AssertReturn(pFObj, STATUS_INVALID_PARAMETER);
    return STATUS_SUCCESS;
}

DECLHIDDEN(NTSTATUS) vboxUsbRtClose(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    Assert(pFObj);

    vboxUsbRtCtxReleaseOwner(pDevExt, pFObj);

    return STATUS_SUCCESS;
}

DECLHIDDEN(NTSTATUS) vboxUsbRtDispatch(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    switch (pSl->Parameters.DeviceIoControl.IoControlCode)
    {
        case SUPUSB_IOCTL_USB_CLAIM_DEVICE:
            return vboxUsbRtDispatchClaimDevice(pDevExt, pIrp);

        case SUPUSB_IOCTL_USB_RELEASE_DEVICE:
            return vboxUsbRtDispatchReleaseDevice(pDevExt, pIrp);

        case SUPUSB_IOCTL_GET_DEVICE:
            return vboxUsbRtDispatchGetDevice(pDevExt, pIrp);

        case SUPUSB_IOCTL_USB_RESET:
            return vboxUsbRtDispatchUsbReset(pDevExt, pIrp);

        case SUPUSB_IOCTL_USB_SET_CONFIG:
            return vboxUsbRtDispatchUsbSetConfig(pDevExt, pIrp);

        case SUPUSB_IOCTL_USB_SELECT_INTERFACE:
            return vboxUsbRtDispatchUsbSelectInterface(pDevExt, pIrp);

        case SUPUSB_IOCTL_USB_CLEAR_ENDPOINT:
            return vboxUsbRtDispatchUsbClearEndpoint(pDevExt, pIrp);

        case SUPUSB_IOCTL_USB_ABORT_ENDPOINT:
            return vboxUsbRtDispatchUsbAbortEndpoint(pDevExt, pIrp);

        case SUPUSB_IOCTL_SEND_URB:
            return vboxUsbRtDispatchSendUrb(pDevExt, pIrp);

        case SUPUSB_IOCTL_IS_OPERATIONAL:
            return vboxUsbRtDispatchIsOperational(pDevExt, pIrp);

        case SUPUSB_IOCTL_GET_VERSION:
            return vboxUsbRtDispatchGetVersion(pDevExt, pIrp);

        default:
            return vboxUsbRtDispatchDefault(pDevExt, pIrp);
    }
}
