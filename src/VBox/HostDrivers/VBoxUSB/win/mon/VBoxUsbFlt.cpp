/* $Id: VBoxUsbFlt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox USB Monitor Device Filtering functionality
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
#include "VBoxUsbMon.h"
#include "../cmn/VBoxUsbTool.h"

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>

#include <iprt/assert.h>

#pragma warning(disable : 4200)
#include "usbdi.h"
#pragma warning(default : 4200)
#include "usbdlib.h"
#include "VBoxUSBFilterMgr.h"
#include <VBox/usblib.h>
#include <devguid.h>
#include <devpkey.h>


/* We should be including ntifs.h but that's not as easy as it sounds. */
extern "C" {
NTKERNELAPI PDEVICE_OBJECT IoGetDeviceAttachmentBaseRef(__in PDEVICE_OBJECT DeviceObject);
}

/*
 * state transitions:
 *
 *           (we are not filtering this device )
 * ADDED --> UNCAPTURED ------------------------------->-
 *       |                                              |
 *       |   (we are filtering this device,             | (the device is being
 *       |    waiting for our device driver             |  re-plugged to perform
 *       |    to pick it up)                            |  capture-uncapture transition)
 *       |-> CAPTURING -------------------------------->|---> REPLUGGING -----
 *            ^  |    (device driver picked             |                    |
 *            |  |     up the device)                   | (remove cased      |  (device is removed
 *            |  ->---> CAPTURED ---------------------->|  by "real" removal |   the device info is removed form the list)
 *            |            |                            |------------------->->--> REMOVED
 *            |            |                            |
 *            |-----------<->---> USED_BY_GUEST ------->|
 *            |                         |
 *            |------------------------<-
 *
 * NOTE: the order of enums DOES MATTER!!
 * Do not blindly modify!! as the code assumes the state is ordered this way.
 */
typedef enum
{
    VBOXUSBFLT_DEVSTATE_UNKNOWN = 0,
    VBOXUSBFLT_DEVSTATE_REMOVED,
    VBOXUSBFLT_DEVSTATE_REPLUGGING,
    VBOXUSBFLT_DEVSTATE_ADDED,
    VBOXUSBFLT_DEVSTATE_UNCAPTURED,
    VBOXUSBFLT_DEVSTATE_CAPTURING,
    VBOXUSBFLT_DEVSTATE_CAPTURED,
    VBOXUSBFLT_DEVSTATE_USED_BY_GUEST,
    VBOXUSBFLT_DEVSTATE_32BIT_HACK = 0x7fffffff
} VBOXUSBFLT_DEVSTATE;

typedef struct VBOXUSBFLT_DEVICE
{
    LIST_ENTRY      GlobalLe;
    /* auxiliary list to be used for gathering devices to be re-plugged
     * only thread that puts the device to the REPLUGGING state can use this list */
    LIST_ENTRY      RepluggingLe;
    /* Owning session. Each matched device has an owning session. */
    struct VBOXUSBFLTCTX *pOwner;
    /* filter id - if NULL AND device has an owner - the filter is destroyed */
    uintptr_t uFltId;
    /* true iff device is filtered with a one-shot filter */
    bool fIsFilterOneShot;
    /* true if descriptors could not be read and only inferred from PnP Manager data */
    bool fInferredDesc;
    /* The device state. If the non-owner session is requesting the state while the device is grabbed,
     * the USBDEVICESTATE_USED_BY_HOST is returned. */
    VBOXUSBFLT_DEVSTATE  enmState;
    volatile uint32_t cRefs;
    PDEVICE_OBJECT  Pdo;
    uint16_t        idVendor;
    uint16_t        idProduct;
    uint16_t        bcdDevice;
    uint16_t        bPort;
    uint8_t         bClass;
    uint8_t         bSubClass;
    uint8_t         bProtocol;
    char            szSerial[MAX_USB_SERIAL_STRING];
    char            szMfgName[MAX_USB_SERIAL_STRING];
    char            szProduct[MAX_USB_SERIAL_STRING];
    WCHAR           szLocationPath[768];
#if 0
    char            szDrvKeyName[512];
    BOOLEAN         fHighSpeed;
#endif
} VBOXUSBFLT_DEVICE, *PVBOXUSBFLT_DEVICE;

#define PVBOXUSBFLT_DEVICE_FROM_LE(_pLe) ( (PVBOXUSBFLT_DEVICE)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(VBOXUSBFLT_DEVICE, GlobalLe) ) )
#define PVBOXUSBFLT_DEVICE_FROM_REPLUGGINGLE(_pLe)  ( (PVBOXUSBFLT_DEVICE)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(VBOXUSBFLT_DEVICE, RepluggingLe) ) )
#define PVBOXUSBFLTCTX_FROM_LE(_pLe) ( (PVBOXUSBFLTCTX)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(VBOXUSBFLTCTX, ListEntry) ) )

typedef struct VBOXUSBFLT_LOCK
{
    KSPIN_LOCK Lock;
    KIRQL OldIrql;
} VBOXUSBFLT_LOCK, *PVBOXUSBFLT_LOCK;

#define VBOXUSBFLT_LOCK_INIT() \
    KeInitializeSpinLock(&g_VBoxUsbFltGlobals.Lock.Lock)
#define VBOXUSBFLT_LOCK_TERM() do { } while (0)
#define VBOXUSBFLT_LOCK_ACQUIRE() \
    KeAcquireSpinLock(&g_VBoxUsbFltGlobals.Lock.Lock, &g_VBoxUsbFltGlobals.Lock.OldIrql);
#define VBOXUSBFLT_LOCK_RELEASE() \
    KeReleaseSpinLock(&g_VBoxUsbFltGlobals.Lock.Lock, g_VBoxUsbFltGlobals.Lock.OldIrql);


typedef struct VBOXUSBFLT_BLDEV
{
    LIST_ENTRY ListEntry;
    uint16_t   idVendor;
    uint16_t   idProduct;
    uint16_t   bcdDevice;
} VBOXUSBFLT_BLDEV, *PVBOXUSBFLT_BLDEV;

#define PVBOXUSBFLT_BLDEV_FROM_LE(_pLe) ( (PVBOXUSBFLT_BLDEV)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(VBOXUSBFLT_BLDEV, ListEntry) ) )

typedef struct VBOXUSBFLTGLOBALS
{
    LIST_ENTRY DeviceList;
    LIST_ENTRY ContextList;
    /* devices known to misbehave */
    LIST_ENTRY BlackDeviceList;
    VBOXUSBFLT_LOCK Lock;
    /** Flag whether to force replugging a device we can't query descirptors from.
     * Short term workaround for @bugref{9479}. */
    ULONG           dwForceReplugWhenDevPopulateFails;
} VBOXUSBFLTGLOBALS, *PVBOXUSBFLTGLOBALS;
static VBOXUSBFLTGLOBALS g_VBoxUsbFltGlobals;

static bool vboxUsbFltBlDevMatchLocked(uint16_t idVendor, uint16_t idProduct, uint16_t bcdDevice)
{
    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.BlackDeviceList.Flink;
            pEntry != &g_VBoxUsbFltGlobals.BlackDeviceList;
            pEntry = pEntry->Flink)
    {
        PVBOXUSBFLT_BLDEV pDev = PVBOXUSBFLT_BLDEV_FROM_LE(pEntry);
        if (pDev->idVendor != idVendor)
            continue;
        if (pDev->idProduct != idProduct)
            continue;
        if (pDev->bcdDevice != bcdDevice)
            continue;

        return true;
    }
    return false;
}

static NTSTATUS vboxUsbFltBlDevAddLocked(uint16_t idVendor, uint16_t idProduct, uint16_t bcdDevice)
{
    if (vboxUsbFltBlDevMatchLocked(idVendor, idProduct, bcdDevice))
        return STATUS_SUCCESS;
    PVBOXUSBFLT_BLDEV pDev = (PVBOXUSBFLT_BLDEV)VBoxUsbMonMemAllocZ(sizeof (*pDev));
    if (!pDev)
    {
        AssertFailed();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pDev->idVendor = idVendor;
    pDev->idProduct = idProduct;
    pDev->bcdDevice = bcdDevice;
    InsertHeadList(&g_VBoxUsbFltGlobals.BlackDeviceList, &pDev->ListEntry);
    return STATUS_SUCCESS;
}

static void vboxUsbFltBlDevClearLocked()
{
    PLIST_ENTRY pNext;
    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.BlackDeviceList.Flink;
            pEntry != &g_VBoxUsbFltGlobals.BlackDeviceList;
            pEntry = pNext)
    {
        pNext = pEntry->Flink;
        VBoxUsbMonMemFree(pEntry);
    }
}

static void vboxUsbFltBlDevPopulateWithKnownLocked()
{
    /* this one halts when trying to get string descriptors from it */
    vboxUsbFltBlDevAddLocked(0x5ac, 0x921c, 0x115);
}


DECLINLINE(void) vboxUsbFltDevRetain(PVBOXUSBFLT_DEVICE pDevice)
{
    Assert(pDevice->cRefs);
    ASMAtomicIncU32(&pDevice->cRefs);
}

static void vboxUsbFltDevDestroy(PVBOXUSBFLT_DEVICE pDevice)
{
    Assert(!pDevice->cRefs);
    Assert(pDevice->enmState == VBOXUSBFLT_DEVSTATE_REMOVED);
    VBoxUsbMonMemFree(pDevice);
}

DECLINLINE(void) vboxUsbFltDevRelease(PVBOXUSBFLT_DEVICE pDevice)
{
    uint32_t cRefs = ASMAtomicDecU32(&pDevice->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
    {
        vboxUsbFltDevDestroy(pDevice);
    }
}

static void vboxUsbFltDevOwnerSetLocked(PVBOXUSBFLT_DEVICE pDevice, PVBOXUSBFLTCTX pContext, uintptr_t uFltId, bool fIsOneShot)
{
    ASSERT_WARN(!pDevice->pOwner, ("device 0x%p has an owner(0x%p)", pDevice, pDevice->pOwner));
    ++pContext->cActiveFilters;
    pDevice->pOwner = pContext;
    pDevice->uFltId = uFltId;
    pDevice->fIsFilterOneShot = fIsOneShot;
}

static void vboxUsbFltDevOwnerClearLocked(PVBOXUSBFLT_DEVICE pDevice)
{
    ASSERT_WARN(pDevice->pOwner, ("no owner for device 0x%p", pDevice));
    --pDevice->pOwner->cActiveFilters;
    ASSERT_WARN(pDevice->pOwner->cActiveFilters < UINT32_MAX/2, ("cActiveFilters (%d)", pDevice->pOwner->cActiveFilters));
    pDevice->pOwner = NULL;
    pDevice->uFltId = 0;
}

static void vboxUsbFltDevOwnerUpdateLocked(PVBOXUSBFLT_DEVICE pDevice, PVBOXUSBFLTCTX pContext, uintptr_t uFltId, bool fIsOneShot)
{
    if (pDevice->pOwner != pContext)
    {
        if (pDevice->pOwner)
            vboxUsbFltDevOwnerClearLocked(pDevice);
        if (pContext)
            vboxUsbFltDevOwnerSetLocked(pDevice, pContext, uFltId, fIsOneShot);
    }
    else if (pContext)
    {
        pDevice->uFltId = uFltId;
        pDevice->fIsFilterOneShot = fIsOneShot;
    }
}

static PVBOXUSBFLT_DEVICE vboxUsbFltDevGetLocked(PDEVICE_OBJECT pPdo)
{
#ifdef VBOX_USB_WITH_VERBOSE_LOGGING
    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_VBoxUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry);
        for (PLIST_ENTRY pEntry2 = pEntry->Flink;
                pEntry2 != &g_VBoxUsbFltGlobals.DeviceList;
                pEntry2 = pEntry2->Flink)
        {
            PVBOXUSBFLT_DEVICE pDevice2 = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry2);
            ASSERT_WARN(    pDevice->idVendor  != pDevice2->idVendor
                    || pDevice->idProduct != pDevice2->idProduct
                    || pDevice->bcdDevice != pDevice2->bcdDevice, ("duplicate devices in a list!!"));
        }
    }
#endif
    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_VBoxUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry);
        ASSERT_WARN(    pDevice->enmState == VBOXUSBFLT_DEVSTATE_REPLUGGING
                || pDevice->enmState == VBOXUSBFLT_DEVSTATE_UNCAPTURED
                || pDevice->enmState == VBOXUSBFLT_DEVSTATE_CAPTURING
                || pDevice->enmState == VBOXUSBFLT_DEVSTATE_CAPTURED
                || pDevice->enmState == VBOXUSBFLT_DEVSTATE_USED_BY_GUEST,
                ("Invalid device state(%d) for device(0x%p) PDO(0x%p)", pDevice->enmState, pDevice, pDevice->Pdo));
        if (pDevice->Pdo == pPdo)
            return pDevice;
    }
    return NULL;
}

static NTSTATUS vboxUsbFltPdoReplug(PDEVICE_OBJECT pDo)
{
    LOG(("Replugging PDO(0x%p)", pDo));
    NTSTATUS Status = VBoxUsbToolIoInternalCtlSendSync(pDo, IOCTL_INTERNAL_USB_CYCLE_PORT, NULL, NULL);
    ASSERT_WARN(Status == STATUS_SUCCESS, ("replugging PDO(0x%p) failed Status(0x%x)", pDo, Status));
    LOG(("Replugging PDO(0x%p) done with Status(0x%x)", pDo, Status));
    return Status;
}

static bool vboxUsbFltDevCanBeCaptured(PVBOXUSBFLT_DEVICE pDevice)
{
    if (pDevice->bClass == USB_DEVICE_CLASS_HUB)
    {
        LOG(("device (0x%p), pdo (0x%p) is a hub, can not be captured", pDevice, pDevice->Pdo));
        return false;
    }
    return true;
}

static PVBOXUSBFLTCTX vboxUsbFltDevMatchLocked(PVBOXUSBFLT_DEVICE pDevice, uintptr_t *puId, bool fRemoveFltIfOneShot, bool *pfFilter, bool *pfIsOneShot)
{
    *puId = 0;
    *pfFilter = false;
    *pfIsOneShot = false;
    if (!vboxUsbFltDevCanBeCaptured(pDevice))
    {
        LOG(("vboxUsbFltDevCanBeCaptured returned false"));
        return NULL;
    }

    USBFILTER DevFlt;
    USBFilterInit(&DevFlt, USBFILTERTYPE_CAPTURE);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_VENDOR_ID, pDevice->idVendor, true);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_PRODUCT_ID, pDevice->idProduct, true);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_REV, pDevice->bcdDevice, true);

    /* If we could not read a string descriptor, don't set the filter item at all. */
    if (pDevice->szMfgName[0])
        USBFilterSetStringExact(&DevFlt, USBFILTERIDX_MANUFACTURER_STR, pDevice->szMfgName, true /*fMustBePresent*/, true /*fPurge*/);
    if (pDevice->szProduct[0])
        USBFilterSetStringExact(&DevFlt, USBFILTERIDX_PRODUCT_STR, pDevice->szProduct, true /*fMustBePresent*/, true /*fPurge*/);
    if (pDevice->szSerial[0])
        USBFilterSetStringExact(&DevFlt, USBFILTERIDX_SERIAL_NUMBER_STR, pDevice->szSerial, true /*fMustBePresent*/, true /*fPurge*/);

    /* If device descriptor had to be inferred from PnP Manager data, the class/subclass/protocol may be wrong.
     * When Windows reports CompatibleIDs 'USB\Class_03&SubClass_00&Prot_00', the device descriptor might be
     * reporting class 3 (HID), *or* the device descriptor might be reporting class 0 (specified by interface)
     * and the device's interface reporting class 3. Ignore the class/subclass/protocol in such case, since
     * we are more or less guaranteed to rely on VID/PID anyway.
     * See @bugref{9479}.
     */
    if (pDevice->fInferredDesc)
    {
        LOG(("Device descriptor was not read, only inferred; ignoring class/subclass/protocol!"));
    }
    else
    {
        LOG(("Setting filter class/subclass/protocol %02X/%02X/%02X\n", pDevice->bClass, pDevice->bSubClass, pDevice->bProtocol));
        USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_CLASS, pDevice->bClass, true);
        USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_SUB_CLASS, pDevice->bSubClass, true);
        USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_PROTOCOL, pDevice->bProtocol, true);
    }

    /* If the port number looks valid, add it to the filter. */
    if (pDevice->bPort)
    {
        LOG(("Setting filter port %04X\n", pDevice->bPort));
        USBFilterSetNumExact(&DevFlt, USBFILTERIDX_PORT, pDevice->bPort, true);
    }
    else
        LOG(("Port number not known, ignoring!"));

    /* Run filters on the thing. */
    PVBOXUSBFLTCTX pOwner = VBoxUSBFilterMatchEx(&DevFlt, puId, fRemoveFltIfOneShot, pfFilter, pfIsOneShot);
    USBFilterDelete(&DevFlt);
    return pOwner;
}

static void vboxUsbFltDevStateMarkReplugLocked(PVBOXUSBFLT_DEVICE pDevice)
{
    vboxUsbFltDevOwnerUpdateLocked(pDevice, NULL, 0, false);
    pDevice->enmState = VBOXUSBFLT_DEVSTATE_REPLUGGING;
}

static bool vboxUsbFltDevStateIsNotFiltered(PVBOXUSBFLT_DEVICE pDevice)
{
    return pDevice->enmState == VBOXUSBFLT_DEVSTATE_UNCAPTURED;
}

static bool vboxUsbFltDevStateIsFiltered(PVBOXUSBFLT_DEVICE pDevice)
{
    return pDevice->enmState >= VBOXUSBFLT_DEVSTATE_CAPTURING;
}

static uint16_t vboxUsbParseHexNumU16(WCHAR **ppStr)
{
    WCHAR       *pStr = *ppStr;
    WCHAR       wc;
    uint16_t    num = 0;
    unsigned    u;

    for (int i = 0; i < 4; ++i)
    {
        if (!*pStr)     /* Just in case the string is too short. */
            break;

        wc = *pStr;
        u = wc >= 'A' ? wc - 'A' + 10 : wc - '0';   /* Hex digit to number. */
        num |= u << (12 - 4 * i);
        pStr++;
    }
    *ppStr = pStr;

    return num;
}

static uint8_t vboxUsbParseHexNumU8(WCHAR **ppStr)
{
    WCHAR       *pStr = *ppStr;
    WCHAR       wc;
    uint16_t    num = 0;
    unsigned    u;

    for (int i = 0; i < 2; ++i)
    {
        if (!*pStr)     /* Just in case the string is too short. */
            break;

        wc = *pStr;
        u = wc >= 'A' ? wc - 'A' + 10 : wc - '0';   /* Hex digit to number. */
        num |= u << (4 - 4 * i);
        pStr++;
    }
    *ppStr = pStr;

    return num;
}

static bool vboxUsbParseHardwareID(WCHAR *pchIdStr, uint16_t *pVid, uint16_t *pPid, uint16_t *pRev)
{
#define VID_PREFIX  L"USB\\VID_"
#define PID_PREFIX  L"&PID_"
#define REV_PREFIX  L"&REV_"

    *pVid = *pPid = *pRev = 0xFFFF;

    /* The Hardware ID is in the format USB\VID_xxxx&PID_xxxx&REV_xxxx, with 'xxxx'
     * being 16-bit hexadecimal numbers. The string is coming from the
     * Windows PnP manager so OEMs should have no opportunity to mess it up.
     */

    if (wcsncmp(pchIdStr, VID_PREFIX, wcslen(VID_PREFIX)))
        return false;
    /* Point to the start of the vendor ID number and parse it. */
    pchIdStr += wcslen(VID_PREFIX);
    *pVid = vboxUsbParseHexNumU16(&pchIdStr);

    if (wcsncmp(pchIdStr, PID_PREFIX, wcslen(PID_PREFIX)))
        return false;
    /* Point to the start of the product ID number and parse it. */
    pchIdStr += wcslen(PID_PREFIX);
    *pPid = vboxUsbParseHexNumU16(&pchIdStr);

    /* The revision might not be there; the Windows documentation is not
     * entirely clear if it will be always present for USB devices or not.
     * If it's not there, still consider this a success. */
    if (wcsncmp(pchIdStr, REV_PREFIX, wcslen(REV_PREFIX)))
        return true;

    /* Point to the start of the revision number and parse it. */
    pchIdStr += wcslen(REV_PREFIX);
    *pRev = vboxUsbParseHexNumU16(&pchIdStr);

    return true;
#undef VID_PREFIX
#undef PID_PREFIX
#undef REV_PREFIX
}

static bool vboxUsbParseCompatibleIDs(WCHAR *pchIdStr, uint8_t *pClass, uint8_t *pSubClass, uint8_t *pProt)
{
#define CLS_PREFIX  L"USB\\Class_"
#define SUB_PREFIX  L"&SubClass_"
#define PRO_PREFIX  L"&Prot_"

    *pClass = *pSubClass = *pProt = 0xFF;

    /* The Compatible IDs string is in the format USB\Class_xx&SubClass_xx&Prot_xx,
     * with 'xx' being 8-bit hexadecimal numbers. Since this string is provided by the
     * PnP manager and USB devices always report these as part of the basic USB device
     * descriptor, we assume all three must be present.
     */

    if (wcsncmp(pchIdStr, CLS_PREFIX, wcslen(CLS_PREFIX)))
        return false;
    /* Point to the start of the device class and parse it. */
    pchIdStr += wcslen(CLS_PREFIX);
    *pClass = vboxUsbParseHexNumU8(&pchIdStr);

    if (wcsncmp(pchIdStr, SUB_PREFIX, wcslen(SUB_PREFIX)))
        return false;

    /* Point to the start of the subclass and parse it. */
    pchIdStr += wcslen(SUB_PREFIX);
    *pSubClass = vboxUsbParseHexNumU8(&pchIdStr);

    if (wcsncmp(pchIdStr, PRO_PREFIX, wcslen(PRO_PREFIX)))
        return false;

    /* Point to the start of the protocol and parse it. */
    pchIdStr += wcslen(PRO_PREFIX);
    *pProt = vboxUsbParseHexNumU8(&pchIdStr);

    return true;
#undef CLS_PREFIX
#undef SUB_PREFIX
#undef PRO_PREFIX
}

#define VBOXUSBMON_POPULATE_REQUEST_TIMEOUT_MS 10000

static NTSTATUS vboxUsbFltDevPopulate(PVBOXUSBFLT_DEVICE pDevice, PDEVICE_OBJECT pDo /*, BOOLEAN bPopulateNonFilterProps*/)
{
    NTSTATUS                Status;
    USB_TOPOLOGY_ADDRESS    TopoAddr;
    PUSB_DEVICE_DESCRIPTOR  pDevDr = 0;
    ULONG                   ulResultLen;
    DEVPROPTYPE             type;
    WCHAR                   wchPropBuf[256];
    uint16_t                port;
    bool                    rc;

    pDevice->Pdo = pDo;

    LOG(("Populating Device(0x%p) for PDO(0x%p)", pDevice, pDo));

    pDevDr = (PUSB_DEVICE_DESCRIPTOR)VBoxUsbMonMemAllocZ(sizeof(*pDevDr));
    if (pDevDr == NULL)
    {
        WARN(("Failed to alloc mem for urb"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    do
    {
        pDevice->fInferredDesc = false;
        Status = VBoxUsbToolGetDescriptor(pDo, pDevDr, sizeof(*pDevDr), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, VBOXUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
        if (!NT_SUCCESS(Status))
        {
            uint16_t    vid, pid, rev;
            uint8_t     cls, sub, prt;

            WARN(("getting device descriptor failed, Status (0x%x); falling back to IoGetDeviceProperty", Status));

            /* Try falling back to IoGetDevicePropertyData. */
            Status = IoGetDevicePropertyData(pDo, &DEVPKEY_Device_HardwareIds, LOCALE_NEUTRAL, 0, sizeof(wchPropBuf), wchPropBuf, &ulResultLen, &type);
            if (!NT_SUCCESS(Status))
            {
                /* This just isn't our day. We have no idea what the device is. */
                WARN(("IoGetDevicePropertyData failed for DEVPKEY_Device_HardwareIds, Status (0x%x)", Status));
                break;
            }
            rc = vboxUsbParseHardwareID(wchPropBuf, &vid, &pid, &rev);
            if (!rc)
            {
                /* This *really* should not happen. */
                WARN(("Failed to parse Hardware ID"));
                break;
            }

            /* Now grab the Compatible IDs to get the class/subclass/protocol. */
            Status = IoGetDevicePropertyData(pDo, &DEVPKEY_Device_CompatibleIds, LOCALE_NEUTRAL, 0, sizeof(wchPropBuf), wchPropBuf, &ulResultLen, &type);
            if (!NT_SUCCESS(Status))
            {
                /* We really kind of need these. */
                WARN(("IoGetDevicePropertyData failed for DEVPKEY_Device_CompatibleIds, Status (0x%x)", Status));
                break;
            }
            rc = vboxUsbParseCompatibleIDs(wchPropBuf, &cls, &sub, &prt);
            if (!rc)
            {
                /* This *really* should not happen. */
                WARN(("Failed to parse Hardware ID"));
                break;
            }

            LOG(("Parsed HardwareID: vid=%04X, pid=%04X, rev=%04X, class=%02X, subcls=%02X, prot=%02X", vid, pid, rev, cls, sub, prt));
            if (vid == 0xFFFF || pid == 0xFFFF)
                break;

            LOG(("Successfully fell back to IoGetDeviceProperty result"));
            pDevDr->idVendor  = vid;
            pDevDr->idProduct = pid;
            pDevDr->bcdDevice = rev;
            pDevDr->bDeviceClass    = cls;
            pDevDr->bDeviceSubClass = sub;
            pDevDr->bDeviceProtocol = prt;

            /* The USB device class/subclass/protocol may not be accurate. We have to be careful when comparing
             * and not take mismatches too seriously.
             */
            pDevice->fInferredDesc = true;
        }

        /* Query the location path. The path is purely a function of the physical device location
         * and does not change if the device changes, and also does not change depending on
         * whether the device is captured or not.
         * NB: We ignore any additional strings and only look at the first one.
         */
        Status = IoGetDevicePropertyData(pDo, &DEVPKEY_Device_LocationPaths, LOCALE_NEUTRAL, 0, sizeof(pDevice->szLocationPath), pDevice->szLocationPath, &ulResultLen, &type);
        if (!NT_SUCCESS(Status))
        {
            /* We do need this, but not critically. On Windows 7, we may get STATUS_OBJECT_NAME_NOT_FOUND. */
            WARN(("IoGetDevicePropertyData failed for DEVPKEY_Device_LocationPaths, Status (0x%x)", Status));
        }
        else
        {
            LOG_STRW(pDevice->szLocationPath);
        }

        // Disabled, but could be used as a fallback instead of IoGetDevicePropertyData; it should work even
        // when this code is entered from the PnP IRP processing path.
#if 0
        {
            HUB_DEVICE_CONFIG_INFO  HubInfo;

            memset(&HubInfo, 0, sizeof(HubInfo));
            HubInfo.Version = 1;
            HubInfo.Length  = sizeof(HubInfo);

            NTSTATUS Status = VBoxUsbToolIoInternalCtlSendSync(pDo, IOCTL_INTERNAL_USB_GET_DEVICE_CONFIG_INFO, &HubInfo, NULL);
            ASSERT_WARN(Status == STATUS_SUCCESS, ("GET_DEVICE_CONFIG_INFO for PDO(0x%p) failed Status(0x%x)", pDo, Status));
            LOG(("Querying hub device config info for PDO(0x%p) done with Status(0x%x)", pDo, Status));

            if (Status == STATUS_SUCCESS)
            {
                uint16_t    vid, pid, rev;
                uint8_t     cls, sub, prt;

                LOG(("Hub flags: %X\n", HubInfo.HubFlags));
                LOG_STRW(HubInfo.HardwareIds.Buffer);
                LOG_STRW(HubInfo.CompatibleIds.Buffer);
                if (HubInfo.DeviceDescription.Buffer)
                    LOG_STRW(HubInfo.DeviceDescription.Buffer);

                rc = vboxUsbParseHardwareID(HubInfo.HardwareIds.Buffer, &pid, &vid, &rev);
                if (!rc)
                {
                    /* This *really* should not happen. */
                    WARN(("Failed to parse Hardware ID"));
                }

                /* The CompatibleID the IOCTL gives is not always the same as what the PnP Manager uses
                 * (thanks, Microsoft). It might look like "USB\DevClass_00&SubClass_00&Prot_00" or like
                 * "USB\USB30_HUB". In such cases, we must consider the class/subclass/protocol
                 * information simply unavailable.
                 */
                rc = vboxUsbParseCompatibleIDs(HubInfo.CompatibleIds.Buffer, &cls, &sub, &prt);
                if (!rc)
                {
                    /* This is unfortunate but not fatal. */
                    WARN(("Failed to parse Compatible ID"));
                }
                LOG(("Parsed HardwareID from IOCTL: vid=%04X, pid=%04X, rev=%04X, class=%02X, subcls=%02X, prot=%02X", vid, pid, rev, cls, sub, prt));

                ExFreePool(HubInfo.HardwareIds.Buffer);
                ExFreePool(HubInfo.CompatibleIds.Buffer);
                if (HubInfo.DeviceDescription.Buffer)
                    ExFreePool(HubInfo.DeviceDescription.Buffer);
            }
        }
#endif

        /* Query the topology address from the hub driver. This is not trivial to translate to the location
         * path, but at least we can get the port number this way.
         */
        memset(&TopoAddr, 0, sizeof(TopoAddr));
        Status = VBoxUsbToolIoInternalCtlSendSync(pDo, IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS, &TopoAddr, NULL);
        ASSERT_WARN(Status == STATUS_SUCCESS, ("GET_TOPOLOGY_ADDRESS for PDO(0x%p) failed Status(0x%x)", pDo, Status));
        LOG(("Querying topology address for PDO(0x%p) done with Status(0x%x)", pDo, Status));

        port = 0;
        if (Status == STATUS_SUCCESS)
        {
            uint16_t    *pPort = &TopoAddr.RootHubPortNumber;

            /* The last non-zero port number is the one we're looking for. It might be on the
             * root hub directly, or on some downstream hub.
             */
            for (int i = 0; i < RT_ELEMENTS(TopoAddr.HubPortNumber) + 1; ++i) {
                if (*pPort)
                    port = *pPort;
                pPort++;
            }
            LOG(("PCI bus/dev/fn: %02X:%02X:%02X, parsed port: %u\n", TopoAddr.PciBusNumber, TopoAddr.PciDeviceNumber, TopoAddr.PciFunctionNumber, port));
            LOG(("RH port: %u, hub ports: %u/%u/%u/%u/%u/%u\n", TopoAddr.RootHubPortNumber, TopoAddr.HubPortNumber[0],
                 TopoAddr.HubPortNumber[1], TopoAddr.HubPortNumber[2], TopoAddr.HubPortNumber[3], TopoAddr.HubPortNumber[4], TopoAddr.HubPortNumber[5]));

            /* In the extremely unlikely case that the port number does not fit into 8 bits, force
             * it to zero to indicate that we can't use it.
             */
            if (port > 255)
                port = 0;
        }

        if (vboxUsbFltBlDevMatchLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice))
        {
            WARN(("found a known black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
            Status = STATUS_UNSUCCESSFUL;
            break;
        }

        LOG(("Device pid=%x vid=%x rev=%x port=%x", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice, port));
        pDevice->bPort        = port;
        pDevice->idVendor     = pDevDr->idVendor;
        pDevice->idProduct    = pDevDr->idProduct;
        pDevice->bcdDevice    = pDevDr->bcdDevice;
        pDevice->bClass       = pDevDr->bDeviceClass;
        pDevice->bSubClass    = pDevDr->bDeviceSubClass;
        pDevice->bProtocol    = pDevDr->bDeviceProtocol;
        pDevice->szSerial[0]  = 0;
        pDevice->szMfgName[0] = 0;
        pDevice->szProduct[0] = 0;

        /* If there are no strings, don't even try to get any string descriptors. */
        if (pDevDr->iSerialNumber || pDevDr->iManufacturer || pDevDr->iProduct)
        {
            int             langId;

            Status = VBoxUsbToolGetLangID(pDo, &langId, VBOXUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
            if (!NT_SUCCESS(Status))
            {
                WARN(("reading language ID failed"));
                if (Status == STATUS_CANCELLED)
                {
                    WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                    vboxUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                    Status = STATUS_UNSUCCESSFUL;
                }
                break;
            }

            if (pDevDr->iSerialNumber)
            {
                Status = VBoxUsbToolGetStringDescriptor(pDo, pDevice->szSerial, sizeof (pDevice->szSerial), pDevDr->iSerialNumber, langId, VBOXUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("reading serial number failed"));
                    ASSERT_WARN(pDevice->szSerial[0] == '\0', ("serial is not zero!!"));
                    if (Status == STATUS_CANCELLED)
                    {
                        WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                        vboxUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                        Status = STATUS_UNSUCCESSFUL;
                        break;
                    }
                    LOG(("pretending success.."));
                    Status = STATUS_SUCCESS;
                }
            }

            if (pDevDr->iManufacturer)
            {
                Status = VBoxUsbToolGetStringDescriptor(pDo, pDevice->szMfgName, sizeof (pDevice->szMfgName), pDevDr->iManufacturer, langId, VBOXUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("reading manufacturer name failed"));
                    ASSERT_WARN(pDevice->szMfgName[0] == '\0', ("szMfgName is not zero!!"));
                    if (Status == STATUS_CANCELLED)
                    {
                        WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                        vboxUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                        Status = STATUS_UNSUCCESSFUL;
                        break;
                    }
                    LOG(("pretending success.."));
                    Status = STATUS_SUCCESS;
                }
            }

            if (pDevDr->iProduct)
            {
                Status = VBoxUsbToolGetStringDescriptor(pDo, pDevice->szProduct, sizeof (pDevice->szProduct), pDevDr->iProduct, langId, VBOXUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("reading product name failed"));
                    ASSERT_WARN(pDevice->szProduct[0] == '\0', ("szProduct is not zero!!"));
                    if (Status == STATUS_CANCELLED)
                    {
                        WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                        vboxUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                        Status = STATUS_UNSUCCESSFUL;
                        break;
                    }
                    LOG(("pretending success.."));
                    Status = STATUS_SUCCESS;
                }
            }

            LOG((": strings: '%s':'%s':'%s' (lang ID %x)",
                        pDevice->szMfgName, pDevice->szProduct, pDevice->szSerial, langId));
        }

        LOG(("Populating Device(0x%p) for PDO(0x%p) Succeeded", pDevice, pDo));
        Status = STATUS_SUCCESS;
    } while (0);

    VBoxUsbMonMemFree(pDevDr);
    LOG(("Populating Device(0x%p) for PDO(0x%p) Done, Status (0x%x)", pDevice, pDo, Status));
    return Status;
}

static bool vboxUsbFltDevCheckReplugLocked(PVBOXUSBFLT_DEVICE pDevice, PVBOXUSBFLTCTX pContext)
{
    ASSERT_WARN(pContext, ("context is NULL!"));

    LOG(("Current context is (0x%p)", pContext));
    LOG(("Current Device owner is (0x%p)", pDevice->pOwner));

    /* check if device is already replugging */
    if (pDevice->enmState <= VBOXUSBFLT_DEVSTATE_ADDED)
    {
        LOG(("Device (0x%p) is already replugging, return..", pDevice));
        /* it is, do nothing */
        ASSERT_WARN(pDevice->enmState == VBOXUSBFLT_DEVSTATE_REPLUGGING,
                ("Device (0x%p) state is NOT REPLUGGING (%d)", pDevice, pDevice->enmState));
        return false;
    }

    if (pDevice->pOwner && pContext != pDevice->pOwner)
    {
        LOG(("Device (0x%p) is owned by another context(0x%p), current is(0x%p)", pDevice, pDevice->pOwner, pContext));
        /* this device is owned by another context, we're not allowed to do anything */
        return false;
    }

    uintptr_t uId = 0;
    bool bNeedReplug = false;
    bool fFilter = false;
    bool fIsOneShot = false;
    PVBOXUSBFLTCTX pNewOwner = vboxUsbFltDevMatchLocked(pDevice, &uId,
            false, /* do not remove a one-shot filter */
            &fFilter, &fIsOneShot);
    LOG(("Matching Info: Filter (0x%p), NewOwner(0x%p), fFilter(%d), fIsOneShot(%d)", uId, pNewOwner, (int)fFilter, (int)fIsOneShot));
    if (pDevice->pOwner && pNewOwner && pDevice->pOwner != pNewOwner)
    {
        LOG(("Matching: Device (0x%p) is requested another owner(0x%p), current is(0x%p)", pDevice, pNewOwner, pDevice->pOwner));
        /* the device is owned by another owner, we can not change the owner here */
        return false;
    }

    if (!fFilter)
    {
        LOG(("Matching: Device (0x%p) should NOT be filtered", pDevice));
        /* the device should NOT be filtered, check the current state  */
        if (vboxUsbFltDevStateIsNotFiltered(pDevice))
        {
            LOG(("Device (0x%p) is NOT filtered", pDevice));
            /* no changes */
            if (fIsOneShot)
            {
                ASSERT_WARN(pNewOwner, ("no new owner"));
                LOG(("Matching: This is a one-shot filter (0x%p), removing..", uId));
                /* remove a one-shot filter and keep the original filter data */
                int tmpRc = VBoxUSBFilterRemove(pNewOwner, uId);
                ASSERT_WARN(RT_SUCCESS(tmpRc), ("remove filter failed, rc (%d)", tmpRc));
                if (!pDevice->pOwner)
                {
                    LOG(("Matching: updating the one-shot owner to (0x%p), fltId(0x%p)", pNewOwner, uId));
                    /* update owner for one-shot if the owner is changed (i.e. assigned) */
                    vboxUsbFltDevOwnerUpdateLocked(pDevice, pNewOwner, uId, true);
                }
                else
                {
                    LOG(("Matching: device already has owner (0x%p) assigned", pDevice->pOwner));
                }
            }
            else
            {
                LOG(("Matching: This is NOT a one-shot filter (0x%p), newOwner(0x%p)", uId, pNewOwner));
                if (pNewOwner)
                {
                    vboxUsbFltDevOwnerUpdateLocked(pDevice, pNewOwner, uId, false);
                }
            }
        }
        else
        {
            LOG(("Device (0x%p) IS filtered", pDevice));
            /* the device is currently filtered, we should release it only if
             * 1. device does not have an owner
             * or
             * 2. it should be released bue to a one-shot filter
             * or
             * 3. it is NOT grabbed by a one-shot filter */
            if (!pDevice->pOwner || fIsOneShot || !pDevice->fIsFilterOneShot)
            {
                LOG(("Matching: Need replug"));
                bNeedReplug = true;
            }
        }
    }
    else
    {
        LOG(("Matching: Device (0x%p) SHOULD be filtered", pDevice));
        /* the device should be filtered, check the current state  */
        ASSERT_WARN(uId, ("zero uid"));
        ASSERT_WARN(pNewOwner, ("zero pNewOwner"));
        if (vboxUsbFltDevStateIsFiltered(pDevice))
        {
            LOG(("Device (0x%p) IS filtered", pDevice));
            /* the device is filtered */
            if (pNewOwner == pDevice->pOwner)
            {
                LOG(("Device owner match"));
                /* no changes */
                if (fIsOneShot)
                {
                    LOG(("Matching: This is a one-shot filter (0x%p), removing..", uId));
                    /* remove a one-shot filter and keep the original filter data */
                    int tmpRc = VBoxUSBFilterRemove(pNewOwner, uId);
                    ASSERT_WARN(RT_SUCCESS(tmpRc), ("remove filter failed, rc (%d)", tmpRc));
                }
                else
                {
                    LOG(("Matching: This is NOT a one-shot filter (0x%p), Owner(0x%p)", uId, pDevice->pOwner));
                    vboxUsbFltDevOwnerUpdateLocked(pDevice, pDevice->pOwner, uId, false);
                }
            }
            else
            {
                ASSERT_WARN(!pDevice->pOwner, ("device should NOT have owner"));
                LOG(("Matching: Need replug"));
                /* the device needs to be filtered, but the owner changes, replug needed */
                bNeedReplug = true;
            }
        }
        else
        {
            /* the device is currently NOT filtered,
             * we should replug it only if
             * 1. device does not have an owner
             * or
             * 2. it should be captured due to a one-shot filter
             * or
             * 3. it is NOT released by a one-shot filter */
            if (!pDevice->pOwner || fIsOneShot || !pDevice->fIsFilterOneShot)
            {
                bNeedReplug = true;
                LOG(("Matching: Need replug"));
            }
        }
    }

    if (bNeedReplug)
    {
        LOG(("Matching: Device needs replugging, marking as such"));
        vboxUsbFltDevStateMarkReplugLocked(pDevice);
    }
    else
    {
        LOG(("Matching: Device does NOT need replugging"));
    }

    return bNeedReplug;
}

static void vboxUsbFltReplugList(PLIST_ENTRY pList)
{
    PLIST_ENTRY pNext;
    for (PLIST_ENTRY pEntry = pList->Flink;
            pEntry != pList;
            pEntry = pNext)
    {
        pNext = pEntry->Flink;
        PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_REPLUGGINGLE(pEntry);
        LOG(("replugging matched PDO(0x%p), pDevice(0x%p)", pDevice->Pdo, pDevice));
        ASSERT_WARN(pDevice->enmState == VBOXUSBFLT_DEVSTATE_REPLUGGING
                || pDevice->enmState == VBOXUSBFLT_DEVSTATE_REMOVED,
                ("invalid state(0x%x) for device(0x%p)", pDevice->enmState, pDevice));

        vboxUsbFltPdoReplug(pDevice->Pdo);
        ObDereferenceObject(pDevice->Pdo);
        vboxUsbFltDevRelease(pDevice);
    }
}

typedef struct VBOXUSBFLTCHECKWALKER
{
    PVBOXUSBFLTCTX pContext;
} VBOXUSBFLTCHECKWALKER, *PVBOXUSBFLTCHECKWALKER;

static DECLCALLBACK(BOOLEAN) vboxUsbFltFilterCheckWalker(PFILE_OBJECT pHubFile,
                                                         PDEVICE_OBJECT pHubDo, PVOID pvContext)
{
    PVBOXUSBFLTCHECKWALKER pData = (PVBOXUSBFLTCHECKWALKER)pvContext;
    PVBOXUSBFLTCTX pContext = pData->pContext;

    LOG(("Visiting pHubFile(0x%p), pHubDo(0x%p), oContext(0x%p)", pHubFile, pHubDo, pContext));
    KIRQL Irql = KeGetCurrentIrql();
    ASSERT_WARN(Irql == PASSIVE_LEVEL, ("unexpected IRQL (%d)", Irql));

    PDEVICE_RELATIONS pDevRelations = NULL;

    NTSTATUS Status = VBoxUsbMonQueryBusRelations(pHubDo, pHubFile, &pDevRelations);
    if (Status == STATUS_SUCCESS && pDevRelations)
    {
        ULONG cReplugPdos = pDevRelations->Count;
        LIST_ENTRY ReplugDevList;
        InitializeListHead(&ReplugDevList);
        for (ULONG k = 0; k < pDevRelations->Count; ++k)
        {
            PDEVICE_OBJECT pDevObj;

            /* Grab the PDO+reference. We won't need the upper layer device object
             * anymore, so dereference that right here, and drop the PDO ref later.
             */
            pDevObj = IoGetDeviceAttachmentBaseRef(pDevRelations->Objects[k]);
            LOG(("DevObj=%p, PDO=%p\n", pDevRelations->Objects[k], pDevObj));
            ObDereferenceObject(pDevRelations->Objects[k]);
            pDevRelations->Objects[k] = pDevObj;

            LOG(("Found existing USB PDO 0x%p", pDevObj));
            VBOXUSBFLT_LOCK_ACQUIRE();
            PVBOXUSBFLT_DEVICE pDevice = vboxUsbFltDevGetLocked(pDevObj);
            if (pDevice)
            {
                LOG(("Found existing device info (0x%p) for PDO 0x%p", pDevice, pDevObj));
                bool bReplug = vboxUsbFltDevCheckReplugLocked(pDevice, pContext);
                if (bReplug)
                {
                    LOG(("Replug needed for device (0x%p)", pDevice));
                    InsertHeadList(&ReplugDevList, &pDevice->RepluggingLe);
                    vboxUsbFltDevRetain(pDevice);
                    /* do not dereference object since we will use it later */
                }
                else
                {
                    LOG(("Replug NOT needed for device (0x%p)", pDevice));
                    ObDereferenceObject(pDevObj);
                }

                VBOXUSBFLT_LOCK_RELEASE();

                pDevRelations->Objects[k] = NULL;
                --cReplugPdos;
                ASSERT_WARN((uint32_t)cReplugPdos < UINT32_MAX/2, ("cReplugPdos(%d) state broken", cReplugPdos));
                continue;
            }
            VBOXUSBFLT_LOCK_RELEASE();

            LOG(("NO device info found for PDO 0x%p", pDevObj));
            VBOXUSBFLT_DEVICE Device;
            Status = vboxUsbFltDevPopulate(&Device, pDevObj /*, FALSE /* only need filter properties */);
            if (NT_SUCCESS(Status))
            {
                uintptr_t uId = 0;
                bool fFilter = false;
                bool fIsOneShot = false;
                VBOXUSBFLT_LOCK_ACQUIRE();
                PVBOXUSBFLTCTX pCtx = vboxUsbFltDevMatchLocked(&Device, &uId,
                                                               false, /* do not remove a one-shot filter */
                                                               &fFilter, &fIsOneShot);
                VBOXUSBFLT_LOCK_RELEASE();
                NOREF(pCtx);
                LOG(("Matching Info: Filter (0x%p), pCtx(0x%p), fFilter(%d), fIsOneShot(%d)", uId, pCtx, (int)fFilter, (int)fIsOneShot));
                if (fFilter)
                {
                    LOG(("Matching: This device SHOULD be filtered"));
                    /* this device needs to be filtered, but it's not,
                     * leave the PDO in array to issue a replug request for it
                     * later on */
                    continue;
                }
            }
            else
            {
                WARN(("vboxUsbFltDevPopulate for PDO 0x%p failed with Status 0x%x", pDevObj, Status));
                if (   Status == STATUS_CANCELLED
                    && g_VBoxUsbFltGlobals.dwForceReplugWhenDevPopulateFails)
                {
                    /*
                     * This can happen if the device got suspended and is in D3 state where we can't query any strings.
                     * There is no known way to set the power state of the device, especially if there is no driver attached yet.
                     * The sledgehammer approach is to just replug the device to force it out of suspend, see bugref @{9479}.
                     */
                    continue;
                }
            }

            LOG(("Matching: This device should NOT be filtered"));
            /* this device should not be filtered, and it's not */
            ObDereferenceObject(pDevObj);
            pDevRelations->Objects[k] = NULL;
            --cReplugPdos;
            ASSERT_WARN((uint32_t)cReplugPdos < UINT32_MAX/2, ("cReplugPdos is %d", cReplugPdos));
        }

        LOG(("(%d) non-matched PDOs to be replugged", cReplugPdos));

        if (cReplugPdos)
        {
            for (ULONG k = 0; k < pDevRelations->Count; ++k)
            {
                if (!pDevRelations->Objects[k])
                    continue;

                Status = vboxUsbFltPdoReplug(pDevRelations->Objects[k]);
                ASSERT_WARN(Status == STATUS_SUCCESS, ("vboxUsbFltPdoReplug failed! Status(0x%x)", Status));
                ObDereferenceObject(pDevRelations->Objects[k]);
                if (!--cReplugPdos)
                    break;
            }

            ASSERT_WARN(!cReplugPdos, ("cReplugPdos reached zero!"));
        }

        vboxUsbFltReplugList(&ReplugDevList);

        ExFreePool(pDevRelations);
    }
    else
    {
        WARN(("VBoxUsbMonQueryBusRelations failed for hub DO(0x%p), Status(0x%x), pDevRelations(0x%p)",
                pHubDo, Status, pDevRelations));
    }

    LOG(("Done Visiting pHubFile(0x%p), pHubDo(0x%p), oContext(0x%p)", pHubFile, pHubDo, pContext));

    return TRUE;
}

NTSTATUS VBoxUsbFltFilterCheck(PVBOXUSBFLTCTX pContext)
{
    KIRQL Irql = KeGetCurrentIrql();
    ASSERT_WARN(Irql == PASSIVE_LEVEL, ("unexpected IRQL (%d)", Irql));

    LOG(("Running filters, Context (0x%p)..", pContext));

    VBOXUSBFLTCHECKWALKER Data;
    Data.pContext = pContext;
    vboxUsbMonHubDevWalk(vboxUsbFltFilterCheckWalker, &Data);

    LOG(("DONE Running filters, Context (0x%p)", pContext));

    return STATUS_SUCCESS;
}

NTSTATUS VBoxUsbFltClose(PVBOXUSBFLTCTX pContext)
{
    LOG(("Closing context(0x%p)", pContext));
    LIST_ENTRY ReplugDevList;
    InitializeListHead(&ReplugDevList);

    ASSERT_WARN(pContext, ("null context"));

    KIRQL Irql = KeGetCurrentIrql();
    ASSERT_WARN(Irql == PASSIVE_LEVEL, ("irql==(%d)", Irql));

    VBOXUSBFLT_LOCK_ACQUIRE();

    pContext->bRemoved = TRUE;
    RemoveEntryList(&pContext->ListEntry);

    LOG(("removing owner filters"));
    /* now re-arrange the filters */
    /* 1. remove filters */
    VBoxUSBFilterRemoveOwner(pContext);

    LOG(("enumerating devices.."));
    /* 2. check if there are devices owned */
    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink;
         pEntry != &g_VBoxUsbFltGlobals.DeviceList;
         pEntry = pEntry->Flink)
    {
        PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry);
        if (pDevice->pOwner != pContext)
            continue;

        LOG(("found device(0x%p), pdo(0x%p), state(%d), filter id(0x%p), oneshot(%d)",
                pDevice, pDevice->Pdo, pDevice->enmState, pDevice->uFltId, (int)pDevice->fIsFilterOneShot));
        ASSERT_WARN(pDevice->enmState != VBOXUSBFLT_DEVSTATE_ADDED, ("VBOXUSBFLT_DEVSTATE_ADDED state for device(0x%p)", pDevice));
        ASSERT_WARN(pDevice->enmState != VBOXUSBFLT_DEVSTATE_REMOVED, ("VBOXUSBFLT_DEVSTATE_REMOVED state for device(0x%p)", pDevice));

        vboxUsbFltDevOwnerClearLocked(pDevice);

        if (vboxUsbFltDevCheckReplugLocked(pDevice, pContext))
        {
            LOG(("device needs replug"));
            InsertHeadList(&ReplugDevList, &pDevice->RepluggingLe);
            /* retain to ensure the device is not removed before we issue a replug */
            vboxUsbFltDevRetain(pDevice);
            /* keep the PDO alive */
            ObReferenceObject(pDevice->Pdo);
        }
        else
        {
            LOG(("device does NOT need replug"));
        }
    }

    VBOXUSBFLT_LOCK_RELEASE();

    /* this should replug all devices that were either skipped or grabbed due to the context's */
    vboxUsbFltReplugList(&ReplugDevList);

    LOG(("SUCCESS done context(0x%p)", pContext));
    return STATUS_SUCCESS;
}

NTSTATUS VBoxUsbFltCreate(PVBOXUSBFLTCTX pContext)
{
    LOG(("Creating context(0x%p)", pContext));
    memset(pContext, 0, sizeof (*pContext));
    pContext->Process = RTProcSelf();
    VBOXUSBFLT_LOCK_ACQUIRE();
    InsertHeadList(&g_VBoxUsbFltGlobals.ContextList, &pContext->ListEntry);
    VBOXUSBFLT_LOCK_RELEASE();
    LOG(("SUCCESS context(0x%p)", pContext));
    return STATUS_SUCCESS;
}

int VBoxUsbFltAdd(PVBOXUSBFLTCTX pContext, PUSBFILTER pFilter, uintptr_t *pId)
{
    LOG(("adding filter, Context (0x%p)..", pContext));
    *pId = 0;
    /* LOG the filter details. */
    LOG((__FUNCTION__": %s %s %s",
        USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  ? USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  : "<null>",
        USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       ? USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       : "<null>",
        USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) ? USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) : "<null>"));
#ifdef VBOX_USB_WITH_VERBOSE_LOGGING
    LOG(("VBoxUSBClient::addFilter: idVendor=%#x idProduct=%#x bcdDevice=%#x bDeviceClass=%#x bDeviceSubClass=%#x bDeviceProtocol=%#x bBus=%#x bPort=%#x Type%#x",
              USBFilterGetNum(pFilter, USBFILTERIDX_VENDOR_ID),
              USBFilterGetNum(pFilter, USBFILTERIDX_PRODUCT_ID),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_REV),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_CLASS),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_SUB_CLASS),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_PROTOCOL),
              USBFilterGetNum(pFilter, USBFILTERIDX_BUS),
              USBFilterGetNum(pFilter, USBFILTERIDX_PORT),
              USBFilterGetFilterType(pFilter)));
#endif

    /* We can't get the bus/port numbers. Ignore them while matching. */
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_BUS, false);
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_PORT, false);

    /* We may not be able to reconstruct the class/subclass/protocol if we aren't able to
     * read the device descriptor. Don't require these to be present. See also the fInferredDesc flag.
     */
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_DEVICE_CLASS, false);
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_DEVICE_SUB_CLASS, false);
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_DEVICE_PROTOCOL, false);

    /* We may also be unable to read string descriptors. Often the userland can't read the
     * string descriptors either because the device is in a low-power state, but it can happen
     * that the userland gets lucky and reads the strings, but by the time we get to read them
     * they're inaccessible due to power management. So, don't require the strings to be present.
     */
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_MANUFACTURER_STR, false);
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_PRODUCT_STR, false);
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR, false);

    uintptr_t uId = 0;
    VBOXUSBFLT_LOCK_ACQUIRE();
    /* Add the filter. */
    int rc = VBoxUSBFilterAdd(pFilter, pContext, &uId);
    VBOXUSBFLT_LOCK_RELEASE();
    if (RT_SUCCESS(rc))
    {
        LOG(("ADDED filter id 0x%p", uId));
        ASSERT_WARN(uId, ("uid is NULL"));
#ifdef VBOX_USBMON_WITH_FILTER_AUTOAPPLY
        VBoxUsbFltFilterCheck();
#endif
    }
    else
    {
        WARN(("VBoxUSBFilterAdd failed rc (%d)", rc));
        ASSERT_WARN(!uId, ("uid is not NULL"));
    }

    *pId = uId;
    return rc;
}

int VBoxUsbFltRemove(PVBOXUSBFLTCTX pContext, uintptr_t uId)
{
    LOG(("removing filter id(0x%p), Context (0x%p)..", pContext, uId));
    Assert(uId);

    VBOXUSBFLT_LOCK_ACQUIRE();
    int rc = VBoxUSBFilterRemove(pContext, uId);
    if (!RT_SUCCESS(rc))
    {
        WARN(("VBoxUSBFilterRemove failed rc (%d)", rc));
        VBOXUSBFLT_LOCK_RELEASE();
        return rc;
    }

    LOG(("enumerating devices.."));
    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_VBoxUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry);
        if (pDevice->fIsFilterOneShot)
        {
            ASSERT_WARN(!pDevice->uFltId, ("oneshot filter on device(0x%p): unexpected uFltId(%d)", pDevice, pDevice->uFltId));
        }

        if (pDevice->uFltId != uId)
            continue;

        ASSERT_WARN(pDevice->pOwner == pContext, ("Device(0x%p) owner(0x%p) not match to (0x%p)", pDevice, pDevice->pOwner, pContext));
        if (pDevice->pOwner != pContext)
            continue;

        LOG(("found device(0x%p), pdo(0x%p), state(%d), filter id(0x%p), oneshot(%d)",
                pDevice, pDevice->Pdo, pDevice->enmState, pDevice->uFltId, (int)pDevice->fIsFilterOneShot));
        ASSERT_WARN(!pDevice->fIsFilterOneShot, ("device(0x%p) is filtered with a oneshot filter", pDevice));
        pDevice->uFltId = 0;
        /* clear the fIsFilterOneShot flag to ensure the device is replugged on the next VBoxUsbFltFilterCheck call */
        pDevice->fIsFilterOneShot = false;
    }
    VBOXUSBFLT_LOCK_RELEASE();

    LOG(("done enumerating devices"));

    if (RT_SUCCESS(rc))
    {
#ifdef VBOX_USBMON_WITH_FILTER_AUTOAPPLY
        VBoxUsbFltFilterCheck();
#endif
    }
    return rc;
}

static USBDEVICESTATE vboxUsbDevGetUserState(PVBOXUSBFLTCTX pContext, PVBOXUSBFLT_DEVICE pDevice)
{
    if (vboxUsbFltDevStateIsNotFiltered(pDevice))
        return USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;

    /* the device is filtered, or replugging */
    if (pDevice->enmState == VBOXUSBFLT_DEVSTATE_REPLUGGING)
    {
        ASSERT_WARN(!pDevice->pOwner, ("replugging device(0x%p) still has an owner(0x%p)", pDevice, pDevice->pOwner));
        ASSERT_WARN(!pDevice->uFltId, ("replugging device(0x%p) still has filter(0x%p)", pDevice, pDevice->uFltId));
        /* no user state for this, we should not return it tu the user */
        return USBDEVICESTATE_USED_BY_HOST;
    }

    /* the device is filtered, if owner differs from the context, return as USED_BY_HOST */
    ASSERT_WARN(pDevice->pOwner, ("device(0x%p) has noowner", pDevice));
    /* the id can be null if a filter is removed */
//    Assert(pDevice->uFltId);

    if (pDevice->pOwner != pContext)
    {
        LOG(("Device owner differs from the current context, returning used by host"));
        return USBDEVICESTATE_USED_BY_HOST;
    }

    switch (pDevice->enmState)
    {
        case VBOXUSBFLT_DEVSTATE_UNCAPTURED:
        case VBOXUSBFLT_DEVSTATE_CAPTURING:
            return USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;
        case VBOXUSBFLT_DEVSTATE_CAPTURED:
            return USBDEVICESTATE_HELD_BY_PROXY;
        case VBOXUSBFLT_DEVSTATE_USED_BY_GUEST:
            return USBDEVICESTATE_USED_BY_GUEST;
        default:
            WARN(("unexpected device state(%d) for device(0x%p)", pDevice->enmState, pDevice));
            return USBDEVICESTATE_UNSUPPORTED;
    }
}

NTSTATUS VBoxUsbFltGetDevice(PVBOXUSBFLTCTX pContext, HVBOXUSBDEVUSR hDevice, PUSBSUP_GETDEV_MON pInfo)
{
    if (!hDevice)
       return STATUS_INVALID_PARAMETER;

    memset (pInfo, 0, sizeof (*pInfo));
    VBOXUSBFLT_LOCK_ACQUIRE();
    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_VBoxUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry);
        Assert(pDevice->enmState != VBOXUSBFLT_DEVSTATE_REMOVED);
        Assert(pDevice->enmState != VBOXUSBFLT_DEVSTATE_ADDED);

        if (pDevice != hDevice)
            continue;

        USBDEVICESTATE enmUsrState = vboxUsbDevGetUserState(pContext, pDevice);
        pInfo->enmState = enmUsrState;
        VBOXUSBFLT_LOCK_RELEASE();
        return STATUS_SUCCESS;
    }

    VBOXUSBFLT_LOCK_RELEASE();

    /* We should not get this far with valid input. */
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS VBoxUsbFltPdoAdd(PDEVICE_OBJECT pPdo, BOOLEAN *pbFiltered)
{
    *pbFiltered = FALSE;
    PVBOXUSBFLT_DEVICE pDevice;

    /* Find the real PDO+reference. Dereference when we're done with it. Note that
     * the input pPdo was not explicitly referenced so we're not dropping its ref.
     */
    PDEVICE_OBJECT pDevObj = IoGetDeviceAttachmentBaseRef(pPdo);
    LOG(("DevObj=%p, real PDO=%p\n", pPdo, pDevObj));
    pPdo = pDevObj;

    /* first check if device is in the a already */
    VBOXUSBFLT_LOCK_ACQUIRE();
    pDevice = vboxUsbFltDevGetLocked(pPdo);
    if (pDevice)
    {
        LOG(("found device (0x%p), state(%d) for PDO(0x%p)", pDevice, pDevice->enmState, pPdo));
        ASSERT_WARN(pDevice->enmState != VBOXUSBFLT_DEVSTATE_ADDED, ("VBOXUSBFLT_DEVSTATE_ADDED state for device(0x%p)", pDevice));
        ASSERT_WARN(pDevice->enmState != VBOXUSBFLT_DEVSTATE_REMOVED, ("VBOXUSBFLT_DEVSTATE_REMOVED state for device(0x%p)", pDevice));
        *pbFiltered = pDevice->enmState >= VBOXUSBFLT_DEVSTATE_CAPTURING;
        VBOXUSBFLT_LOCK_RELEASE();
        ObDereferenceObject(pPdo);
        return STATUS_SUCCESS;
    }
    VBOXUSBFLT_LOCK_RELEASE();
    pDevice = (PVBOXUSBFLT_DEVICE)VBoxUsbMonMemAllocZ(sizeof (*pDevice));
    if (!pDevice)
    {
        WARN(("VBoxUsbMonMemAllocZ failed"));
        ObDereferenceObject(pPdo);
        return STATUS_NO_MEMORY;
    }

    pDevice->enmState = VBOXUSBFLT_DEVSTATE_ADDED;
    pDevice->cRefs = 1;
    NTSTATUS Status = vboxUsbFltDevPopulate(pDevice, pPdo /* , TRUE /* need all props */);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxUsbFltDevPopulate failed, Status 0x%x", Status));
        ObDereferenceObject(pPdo);
        VBoxUsbMonMemFree(pDevice);
        return Status;
    }

    uintptr_t uId;
    bool fFilter = false;
    bool fIsOneShot = false;
    PVBOXUSBFLTCTX pCtx;
    PVBOXUSBFLT_DEVICE pTmpDev;
    VBOXUSBFLT_LOCK_ACQUIRE();
    /* (paranoia) re-check the device is still not here */
    pTmpDev = vboxUsbFltDevGetLocked(pPdo);

    /* Drop the PDO ref, now we won't need it anymore. */
    ObDereferenceObject(pPdo);

    if (pTmpDev)
    {
        LOG(("second try: found device (0x%p), state(%d) for PDO(0x%p)", pDevice, pDevice->enmState, pPdo));
        ASSERT_WARN(pDevice->enmState != VBOXUSBFLT_DEVSTATE_ADDED, ("second try: VBOXUSBFLT_DEVSTATE_ADDED state for device(0x%p)", pDevice));
        ASSERT_WARN(pDevice->enmState != VBOXUSBFLT_DEVSTATE_REMOVED, ("second try: VBOXUSBFLT_DEVSTATE_REMOVED state for device(0x%p)", pDevice));
        *pbFiltered = pTmpDev->enmState >= VBOXUSBFLT_DEVSTATE_CAPTURING;
        VBOXUSBFLT_LOCK_RELEASE();
        VBoxUsbMonMemFree(pDevice);
        return STATUS_SUCCESS;
    }

    LOG(("Created Device 0x%p for PDO 0x%p", pDevice, pPdo));

    pCtx = vboxUsbFltDevMatchLocked(pDevice, &uId,
            true, /* remove a one-shot filter */
            &fFilter, &fIsOneShot);
    LOG(("Matching Info: Filter (0x%p), pCtx(0x%p), fFilter(%d), fIsOneShot(%d)", uId, pCtx, (int)fFilter, (int)fIsOneShot));
    if (fFilter)
    {
        LOG(("Created Device 0x%p should be filtered", pDevice));
        ASSERT_WARN(pCtx, ("zero ctx"));
        ASSERT_WARN(uId, ("zero uId"));
        pDevice->enmState = VBOXUSBFLT_DEVSTATE_CAPTURING;
    }
    else
    {
        LOG(("Created Device 0x%p should NOT be filtered", pDevice));
        ASSERT_WARN(!uId == !pCtx, ("invalid uid(0x%p) - ctx(0x%p) pair", uId, pCtx)); /* either both zero or both not */
        pDevice->enmState = VBOXUSBFLT_DEVSTATE_UNCAPTURED;
    }

    if (pCtx)
        vboxUsbFltDevOwnerSetLocked(pDevice, pCtx, fIsOneShot ? 0 : uId, fIsOneShot);

    InsertHeadList(&g_VBoxUsbFltGlobals.DeviceList, &pDevice->GlobalLe);

    /* do not need to signal anything here -
     * going to do that once the proxy device object starts */
    VBOXUSBFLT_LOCK_RELEASE();

    *pbFiltered = fFilter;

    return STATUS_SUCCESS;
}

BOOLEAN VBoxUsbFltPdoIsFiltered(PDEVICE_OBJECT pPdo)
{
    VBOXUSBFLT_DEVSTATE enmState = VBOXUSBFLT_DEVSTATE_REMOVED;

    /* Find the real PDO+reference. Dereference when we're done with it. Note that
     * the input pPdo was not explicitly referenced so we're not dropping its ref.
     */
    PDEVICE_OBJECT pDevObj = IoGetDeviceAttachmentBaseRef(pPdo);
    LOG(("DevObj=%p, real PDO=%p\n", pPdo, pDevObj));
    pPdo = pDevObj;

    VBOXUSBFLT_LOCK_ACQUIRE();

    PVBOXUSBFLT_DEVICE pDevice = vboxUsbFltDevGetLocked(pPdo);
    if (pDevice)
        enmState = pDevice->enmState;

    VBOXUSBFLT_LOCK_RELEASE();
    ObDereferenceObject(pPdo);

    return enmState >= VBOXUSBFLT_DEVSTATE_CAPTURING;
}

NTSTATUS VBoxUsbFltPdoRemove(PDEVICE_OBJECT pPdo)
{
    PVBOXUSBFLT_DEVICE pDevice;
    VBOXUSBFLT_DEVSTATE enmOldState;

    /* Find the real PDO+reference. Dereference when we're done with it. Note that
     * the input pPdo was not explicitly referenced so we're not dropping its ref.
     */
    PDEVICE_OBJECT pDevObj = IoGetDeviceAttachmentBaseRef(pPdo);
    LOG(("DevObj=%p, real PDO=%p\n", pPdo, pDevObj));
    pPdo = pDevObj;

    VBOXUSBFLT_LOCK_ACQUIRE();
    pDevice = vboxUsbFltDevGetLocked(pPdo);
    if (pDevice)
    {
        RemoveEntryList(&pDevice->GlobalLe);
        enmOldState = pDevice->enmState;
        pDevice->enmState = VBOXUSBFLT_DEVSTATE_REMOVED;
    }
    VBOXUSBFLT_LOCK_RELEASE();
    ObDereferenceObject(pPdo);
    if (pDevice)
        vboxUsbFltDevRelease(pDevice);
    return STATUS_SUCCESS;
}

HVBOXUSBFLTDEV VBoxUsbFltProxyStarted(PDEVICE_OBJECT pPdo)
{
    PVBOXUSBFLT_DEVICE pDevice;
    VBOXUSBFLT_LOCK_ACQUIRE();

    /* NB: The USB proxy (VBoxUSB.sys) passes us the real PDO, not anything above that. */
    pDevice = vboxUsbFltDevGetLocked(pPdo);
    /*
     * Prevent a host crash when vboxUsbFltDevGetLocked fails to locate the matching PDO
     * in g_VBoxUsbFltGlobals.DeviceList (see @bugref{6509}).
     */
    if (pDevice == NULL)
    {
        WARN(("failed to get device for PDO(0x%p)", pPdo));
    }
    else if (pDevice->enmState == VBOXUSBFLT_DEVSTATE_CAPTURING)
    {
        pDevice->enmState = VBOXUSBFLT_DEVSTATE_CAPTURED;
        LOG(("The proxy notified proxy start for the captured device 0x%p", pDevice));
        vboxUsbFltDevRetain(pDevice);
    }
    else
    {
        WARN(("invalid state, %d", pDevice->enmState));
        pDevice = NULL;
    }
    VBOXUSBFLT_LOCK_RELEASE();
    return pDevice;
}

void VBoxUsbFltProxyStopped(HVBOXUSBFLTDEV hDev)
{
    PVBOXUSBFLT_DEVICE pDevice = (PVBOXUSBFLT_DEVICE)hDev;
    /*
     * Prevent a host crash when VBoxUsbFltProxyStarted fails, returning NULL.
     * See @bugref{6509}.
     */
    if (pDevice == NULL)
    {
        WARN(("VBoxUsbFltProxyStopped called with NULL device pointer"));
        return;
    }
    VBOXUSBFLT_LOCK_ACQUIRE();
    if (pDevice->enmState == VBOXUSBFLT_DEVSTATE_CAPTURED
            || pDevice->enmState == VBOXUSBFLT_DEVSTATE_USED_BY_GUEST)
    {
        /* this is due to devie was physically removed */
        LOG(("The proxy notified proxy stop for the captured device 0x%p, current state %d", pDevice, pDevice->enmState));
        pDevice->enmState = VBOXUSBFLT_DEVSTATE_CAPTURING;
    }
    else
    {
        if (pDevice->enmState != VBOXUSBFLT_DEVSTATE_REPLUGGING)
        {
            WARN(("invalid state, %d", pDevice->enmState));
        }
    }
    VBOXUSBFLT_LOCK_RELEASE();

    vboxUsbFltDevRelease(pDevice);
}


static NTSTATUS vboxUsbFltRegKeyQuery(PWSTR ValueName, ULONG ValueType, PVOID ValueData, ULONG ValueLength, PVOID Context, PVOID EntryContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RT_NOREF(ValueName, Context);
    if (   ValueType == REG_DWORD
        && ValueLength == sizeof(ULONG))
        *(ULONG *)EntryContext = *(ULONG *)ValueData;
    else
        Status = STATUS_OBJECT_TYPE_MISMATCH;

    return Status;
}


NTSTATUS VBoxUsbFltInit()
{
    int rc = VBoxUSBFilterInit();
    if (RT_FAILURE(rc))
    {
        WARN(("VBoxUSBFilterInit failed, rc (%d)", rc));
        return STATUS_UNSUCCESSFUL;
    }

    memset(&g_VBoxUsbFltGlobals, 0, sizeof (g_VBoxUsbFltGlobals));
    InitializeListHead(&g_VBoxUsbFltGlobals.DeviceList);
    InitializeListHead(&g_VBoxUsbFltGlobals.ContextList);
    InitializeListHead(&g_VBoxUsbFltGlobals.BlackDeviceList);
    vboxUsbFltBlDevPopulateWithKnownLocked();
    VBOXUSBFLT_LOCK_INIT();

    /*
     * Check whether the setting to force replugging USB devices when
     * querying string descriptors fail is set in the registry,
     * see @bugref{9479}.
     */
    RTL_QUERY_REGISTRY_TABLE aParams[] =
    {
        {vboxUsbFltRegKeyQuery, 0, (PWSTR)L"ForceReplugWhenDevPopulateFails", &g_VBoxUsbFltGlobals.dwForceReplugWhenDevPopulateFails, REG_DWORD, &g_VBoxUsbFltGlobals.dwForceReplugWhenDevPopulateFails, sizeof(ULONG) },
        {                 NULL, 0,                                      NULL,                                                   NULL,         0,                                                      0,             0 }
    };
    UNICODE_STRING UnicodePath = RTL_CONSTANT_STRING(L"\\VBoxUSB");

    NTSTATUS Status = RtlQueryRegistryValues(RTL_REGISTRY_CONTROL, UnicodePath.Buffer, &aParams[0], NULL, NULL);
    if (Status == STATUS_SUCCESS)
    {
        if (g_VBoxUsbFltGlobals.dwForceReplugWhenDevPopulateFails)
            LOG(("Forcing replug of USB devices where querying the descriptors fail\n"));
    }
    else
        LOG(("RtlQueryRegistryValues() -> %#x, assuming defaults\n", Status));

    return STATUS_SUCCESS;
}

NTSTATUS VBoxUsbFltTerm()
{
    bool bBusy = false;
    VBOXUSBFLT_LOCK_ACQUIRE();
    do
    {
        if (!IsListEmpty(&g_VBoxUsbFltGlobals.ContextList))
        {
            AssertFailed();
            bBusy = true;
            break;
        }

        PLIST_ENTRY pNext = NULL;
        for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink;
                pEntry != &g_VBoxUsbFltGlobals.DeviceList;
                pEntry = pNext)
        {
            pNext = pEntry->Flink;
            PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry);
            Assert(!pDevice->uFltId);
            Assert(!pDevice->pOwner);
            if (pDevice->cRefs != 1)
            {
                AssertFailed();
                bBusy = true;
                break;
            }
        }
    } while (0);

    VBOXUSBFLT_LOCK_RELEASE()

    if (bBusy)
    {
        return STATUS_DEVICE_BUSY;
    }

    for (PLIST_ENTRY pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_VBoxUsbFltGlobals.DeviceList;
            pEntry = g_VBoxUsbFltGlobals.DeviceList.Flink)
    {
        RemoveEntryList(pEntry);
        PVBOXUSBFLT_DEVICE pDevice = PVBOXUSBFLT_DEVICE_FROM_LE(pEntry);
        pDevice->enmState = VBOXUSBFLT_DEVSTATE_REMOVED;
        vboxUsbFltDevRelease(pDevice);
    }

    vboxUsbFltBlDevClearLocked();

    VBOXUSBFLT_LOCK_TERM();

    VBoxUSBFilterTerm();

    return STATUS_SUCCESS;
}

